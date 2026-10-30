// ============================================================================
//  ScriptBuild.cpp - finding a compiler and using it. See ScriptBuild.h.
//
//  ==========================================================================
//  HOW THE COMPILER IS ACTUALLY RUN
//
//  The editor writes a small build script into .build/ and runs it, with
//  everything the compiler prints redirected into a log file it then reads
//  back.
//
//  That is deliberately low-tech, and it has one property worth the trouble:
//  the build script is a real file that stays on disk afterwards. When a build
//  does something surprising, you can open .build/build.bat and read the exact
//  command that ran, or run it yourself in a terminal. A build step hidden
//  inside the program would give you nothing to look at.
//
//  It runs with NO CONSOLE WINDOW. That is not a cosmetic choice - a window
//  that appears mid-edit is a window people close, and closing it kills the
//  compiler half way through. See RunCaptured.
//
//  ==========================================================================
//  WHAT COUNTS AS A SCRIPT
//
//  Every .cpp under assets/, at any depth, plus every .h that registers one.
//  There is no designated scripts folder: you arrange your project the way it
//  suits the game - enemies/, player/, ui/ - and the scripts live next to the
//  scenes and images they belong with.
// ============================================================================

#if defined(_MSC_VER)
// Microsoft's compiler warns that std::getenv is "unsafe" and suggests a
// Microsoft-only replacement. std::getenv is standard C++ and is used
// correctly below - its result is read immediately and never kept - so the
// warning is switched off here rather than making the code work on only one
// compiler. The same note is in engine/src/fs/FileSystem.cpp.
#pragma warning(disable : 4996)
#endif

#include "ScriptBuild.h"

#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>
#include <engine/project/EngineBuild.h>
#include <engine/project/ScriptAbi.h>
#include <engine/scene/ScriptComponent.h>
#include <engine/core/ThreadPool.h>
#include <engine/scene/ScriptLibrary.h>
#include <engine/tools/ScriptVars.h>

#include <cstdint>
#include <cstring>
#include <cctype>

// SDL is used for one thing: asking where the editor's own executable is, so a
// released copy can find the headers it ships beside itself. The editor
// already links SDL for ImGui's backend, so this costs nothing extra.
#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

// Windows only, and for one job: running the compiler without a console window
// appearing. See RunCaptured.
//
// WIN32_LEAN_AND_MEAN drops the half of windows.h nothing here uses, and
// NOMINMAX stops it defining min and max as MACROS, which would then break the
// std::max and std::min this file uses. The same two lines, for the same two
// reasons, are in editor/src/main.cpp.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace editor {
namespace {

namespace fs = std::filesystem;

// Filled in once by Init().
std::string g_compilerDescription;
std::string g_compilerPath;      // cl.exe, g++ or clang++
std::string g_vcvarsPath;        // Windows only; empty when cl is already usable
bool        g_haveCompiler = false;

std::string g_lastOutput;

// ---------------------------------------------------------------------------
//  Small helpers
// ---------------------------------------------------------------------------

std::string ReadWholeFile(const fs::path& path) {
    std::ifstream file(path);
    if (!file) {
        return {};
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

// Runs a command with everything it prints sent to `logPath`, and returns the
// exit code. Zero means success, as it does for every command-line tool.
int RunCaptured(const std::string& command, const fs::path& logPath) {
#if !defined(_WIN32)
    const std::string full = command + " > \"" + logPath.string() + "\" 2>&1";
    return std::system(full.c_str());
#else
    // ========================================================================
    //  NO CONSOLE WINDOW, which is the whole reason this is not std::system.
    //
    //  std::system hands the line to cmd.exe, and the editor is a GUI program
    //  with no console of its own - so Windows MAKES one for every build. It
    //  opens over whatever you were reading, takes the keyboard, and goes
    //  away again when the compiler finishes.
    //
    //  THAT IS WORSE THAN UNTIDY, for two reasons that both cost real time:
    //
    //    It is a window, so people close it - and closing it kills the
    //    compiler part way through a build that has ALREADY deleted the
    //    objects it was about to replace. What comes back is "these scripts
    //    did not compile" with no error explaining why, because the compiler
    //    never got to complain about anything.
    //
    //    A window opening and closing MOVES THE FOCUS, and the editor checks
    //    for changed scripts when it regains focus. After a failed build that
    //    check used to say yes every time, so the flash started another
    //    build, which flashed again - a loop that fed itself and buried the
    //    error that started it. See g_failedInputs for the other half of
    //    that fix.
    //
    //  So the child is started directly with CREATE_NO_WINDOW, and handed the
    //  log file as its stdout and stderr. That is the same redirect the
    //  command line used to carry - arranged by handle instead of by asking
    //  cmd.exe to do it.
    // ========================================================================
    SECURITY_ATTRIBUTES inherit{};
    inherit.nLength        = sizeof(inherit);
    inherit.bInheritHandle = TRUE;   // the child cannot be given a handle otherwise

    const HANDLE log = CreateFileA(logPath.string().c_str(), GENERIC_WRITE,
                                   FILE_SHARE_READ, &inherit, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        // There is nowhere to write the reason, so do it the old way instead.
        // A console window is a far smaller problem than a build that cannot
        // run at all - and the quoting below is why: cmd strips one leading
        // and one trailing quote, so the outer pair is what keeps a path with
        // spaces in it from being mis-read as two arguments.
        const std::string full =
            "\"" + command + " > \"" + logPath.string() + "\" 2>&1\"";
        return std::system(full.c_str());
    }

    // Nothing in a build reads from stdin, but a child with NO stdin at all
    // can make a tool fail in a confusing way rather than simply read nothing,
    // so it gets the null device.
    const HANDLE nul =
        CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOA start{};
    start.cb         = sizeof(start);
    start.dwFlags    = STARTF_USESTDHANDLES;
    start.hStdInput  = nul;
    start.hStdOutput = log;
    start.hStdError  = log;

    // THROUGH cmd.exe STILL, because the thing being run is a batch file and
    // CreateProcess runs programs, not scripts. /c is "run this and exit".
    //
    // /s AND THE EXTRA QUOTES GO TOGETHER. cmd's rules for which quotes it
    // strips are famously conditional; /s makes it unconditional - remove the
    // first and last quote of what follows /c and run the rest verbatim. So
    // the outer pair added here is consumed by cmd and the command arrives
    // exactly as it was passed in, spaces in paths and all. This is the same
    // arrangement std::system needed above, made explicit.
    const std::string line = "cmd.exe /s /c \"" + command + "\"";

    // COPIED into a buffer of its own: CreateProcess is documented to modify
    // the command line it is given, and doing that to a std::string's
    // internals is undefined behaviour.
    std::vector<char> writable(line.begin(), line.end());
    writable.push_back('\0');

    PROCESS_INFORMATION child{};
    const BOOL          started =
        CreateProcessA(nullptr, writable.data(), nullptr, nullptr,
                       /*bInheritHandles=*/TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                       &start, &child);

    DWORD exitCode = 1;   // anything non-zero: "this did not work"
    if (started) {
        WaitForSingleObject(child.hProcess, INFINITE);
        GetExitCodeProcess(child.hProcess, &exitCode);
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
    } else {
        // SAID INTO THE LOG, because that is the file the editor reads back
        // and shows after a failed build. A silent non-zero here would look
        // exactly like a compiler that printed nothing.
        const std::string why =
            "the editor could not start the compiler: CreateProcess failed with "
            "Windows error " + std::to_string(GetLastError()) + "\r\n";
        DWORD written = 0;
        WriteFile(log, why.c_str(), static_cast<DWORD>(why.size()), &written, nullptr);
    }

    if (nul != INVALID_HANDLE_VALUE) {
        CloseHandle(nul);
    }
    CloseHandle(log);
    return static_cast<int>(exitCode);
#endif
}

// The folder the editor's executable is in. A released copy of the editor
// ships the engine's headers beside itself, so this is where to look first.
fs::path ExecutableDirectory() {
    if (const char* base = SDL_GetBasePath(); base != nullptr && base[0] != '\0') {
        return fs::path(base);
    }
    return {};
}

// Where the engine's headers are.
//
// Two places, in order: beside the editor (a released install), then the path
// baked in when the editor was built (running from this source tree). Checking
// the shipped copy first means a released editor never depends on a source
// tree that may not be there.
std::vector<fs::path> IncludeDirectories() {
    std::vector<fs::path> result;

    const fs::path shipped = ExecutableDirectory() / "include";
    std::error_code ec;
    if (fs::is_directory(shipped, ec)) {
        result.push_back(shipped);
        return result;
    }

#ifdef ENGINE_SCRIPT_INCLUDE_DIR
    result.emplace_back(ENGINE_SCRIPT_INCLUDE_DIR);
#endif

    // nlohmann/json is part of the engine's public interface - every
    // component's Deserialize takes a Json - so a script needs its headers
    // too. CMake can hand back several paths separated by semicolons.
#ifdef ENGINE_SCRIPT_JSON_INCLUDE_DIR
    {
        std::stringstream paths{std::string(ENGINE_SCRIPT_JSON_INCLUDE_DIR)};
        std::string       one;
        while (std::getline(paths, one, ';')) {
            if (!one.empty()) {
                result.emplace_back(one);
            }
        }
    }
#endif
    return result;
}

// The library a script links against: engine.lib on Windows, libengine.so
// elsewhere. Beside the editor in a released install, otherwise where this
// build put it.
fs::path EngineLinkLibrary() {
    std::error_code ec;

#if defined(_WIN32)
    const fs::path shipped = ExecutableDirectory() / "engine.lib";
#else
    const fs::path shipped = ExecutableDirectory() / "libengine.so";
#endif
    if (fs::exists(shipped, ec)) {
        return shipped;
    }

#ifdef ENGINE_SCRIPT_IMPORT_LIB
    return fs::path(ENGINE_SCRIPT_IMPORT_LIB);
#else
    return {};
#endif
}

// ---------------------------------------------------------------------------
//  Is the engine library older than the engine headers beside it?
//
//  THE FAILURE THIS EXISTS TO EXPLAIN, because it is a genuinely confusing one.
//
//  A script calls eng::Physics2D::Raycast. The header declares it, so the
//  script COMPILES. The link then fails with "unresolved external symbol"
//  naming a dozen eng:: functions, and the editor reports it as "the scripts
//  did not compile" - so it reads like broken script code, or a broken engine.
//
//  Nothing is wrong with either. The library was simply built before those
//  functions existed. It happens every time the engine is changed and only ONE
//  configuration is rebuilt: Debug and Release have their own engine.lib, and
//  building one leaves the other behind without anything saying so.
//
//  CurrentBuildStamp already catches "you have switched configuration", which
//  is a different question - it compares WHICH library, not HOW OLD it is.
// ---------------------------------------------------------------------------

// The most recently written header anywhere the scripts include from.
bool NewestEngineHeader(fs::path& outPath, fs::file_time_type& outTime) {
    bool            found = false;
    std::error_code ec;

    for (const fs::path& root : IncludeDirectories()) {
        if (!fs::is_directory(root, ec)) {
            continue;
        }
        for (fs::recursive_directory_iterator it(root, ec), end; it != end;
             it.increment(ec)) {
            if (ec) {
                break;
            }
            if (!it->is_regular_file(ec)) {
                continue;
            }
            const fs::path&   path = it->path();
            const std::string ext  = path.extension().string();
            if (ext != ".h" && ext != ".hpp") {
                continue;
            }
            const auto when = fs::last_write_time(path, ec);
            if (ec) {
                ec.clear();
                continue;
            }
            if (!found || when > outTime) {
                outPath = path;
                outTime = when;
                found   = true;
            }
        }
        ec.clear();
    }
    return found;
}

// True when the library scripts link against predates the headers they are
// compiled against. `outDetail` names both files, because "rebuild the engine"
// is not actionable without knowing WHICH build of it is behind.
bool EngineLibraryIsStale(std::string& outDetail) {
    const fs::path  library = EngineLinkLibrary();
    std::error_code ec;
    if (library.empty() || !fs::exists(library, ec)) {
        return false;
    }

    const auto libraryTime = fs::last_write_time(library, ec);
    if (ec) {
        return false;
    }

    fs::path           header;
    fs::file_time_type headerTime{};
    if (!NewestEngineHeader(header, headerTime)) {
        return false;
    }
    if (headerTime <= libraryTime) {
        return false;
    }

    outDetail = header.string() + "' is newer than '" + library.string();
    return true;
}

fs::path BuildDirectory() {
    return fs::path(eng::FileSystem::Resolve(".build"));
}

fs::path OutputLibrary() {
    return fs::path(eng::FileSystem::Resolve(eng::ScriptLibrary::DefaultVirtualPath()));
}

// The file that records which sources went into the current library.
//
// Timestamps alone cannot notice a DELETED script: every remaining source
// would still be older than the library, so nothing would look out of date and
// the deleted script would go on running. Comparing the list catches that.
fs::path SourceListFile() {
    return BuildDirectory() / "sources.txt";
}

// The configuration the current library was built for. See CurrentBuildStamp.
//
// THE NAME COMES FROM THE ENGINE, because the engine reads this file too -
// the player checks it at start-up and refuses to load scripts built for a
// different engine. A filename spelled in two places is a filename that can
// be changed in one.
fs::path BuildStampFile() {
    return fs::path(eng::FileSystem::Resolve(eng::ScriptAbi::StampVirtualPath()));
}

// How the current build would compile scripts, as a short piece of text to
// compare against the last build.
//
// WHY THIS EXISTS: a script library is only usable by an engine built the same
// way. Debug and Release use different C runtimes, and a script compiled
// against one and loaded into the other is a program with two heaps in it -
// it loads successfully, reports success, and then crashes on the first
// allocation that crosses the boundary.
//
// That is not hypothetical. Before this check, exporting a Release game picked
// up a Debug-built library, the log said "loaded 9 script(s)", and the game
// died with an access violation before it drew a frame. Nothing in the source
// set or the timestamps had changed, so nothing thought a rebuild was needed.
//
// The engine library's PATH is in here as well as the runtime flavour, because
// the Debug and Release builds put engine.lib in different folders - so this
// one string catches both "the configuration changed" and "you are pointing at
// a different build of the engine".
std::string CurrentBuildStamp() {
    // ======================================================================
    //  MOST OF THIS NOW COMES FROM THE ENGINE, and that move is the point.
    //
    //  eng::ScriptAbi::Stamp() is the authority on what a script must have
    //  been built against in order to be LOADABLE: the C runtime flavour,
    //  the engine build's identity, and the script ABI number.
    //
    //  The editor is not the only thing that needs to know. The PLAYER HAS NO
    //  COMPILER, so when it meets scripts built by the other configuration it
    //  cannot rebuild them - its only choices are to load them and corrupt
    //  the heap, or to refuse and say so. It now refuses, by comparing
    //  against this same text. Two copies of "what makes a script loadable"
    //  would drift, and the first symptom would be one program rebuilding
    //  for a difference the other ignores. See engine/project/ScriptAbi.h.
    //
    //  WHAT CATCHES WHAT, since the lines look interchangeable and are not:
    //
    //    engine= and abi= catch an engine REPLACED IN PLACE, which is exactly
    //      what installing a new version over an old one is. Nothing about
    //      the scripts changed and the library path below is the same string,
    //      so without these every object looks current while having been
    //      compiled against different headers. Compared rather than dated, so
    //      a DOWNGRADE is caught too - every other staleness test in this
    //      file is a one-directional timestamp comparison.
    //
    //    crt= catches Debug against Release, which is the one somebody hits
    //      by accident on an ordinary afternoon.
    //
    //  An engine with no engine-version.json reports "unknown" and abi 0.
    //  That is stable, so a hand-built engine rebuilds no more often than it
    //  did before any of this existed.
    // ======================================================================
    std::string stamp = eng::ScriptAbi::Stamp();

    // AND ONE LINE OF THE EDITOR'S OWN. The engine cannot check this - it has
    // no way to know which spelling of its own location a build linked
    // against - but the editor can and should: two engine builds in different
    // folders are told apart by it. ScriptAbi::Check ignores keys it does not
    // know, which is what lets both layers keep their own facts in one file.
    stamp += "enginelib=" + EngineLinkLibrary().string() + "\n";
    return stamp;
}

// Which class names each file registered, as of the last successful build.
//
// This is what lets a rebuild notice that a file which used to define `Player`
// now defines `PlayerController`. Without it, renaming a class in your editor
// would quietly turn every entity using it into an unresolved script, and the
// only symptom would be that the game stopped doing something.
fs::path ClassManifestFile() {
    return BuildDirectory() / "classes.txt";
}

// The one generated file: a translation unit that includes every script
// written in a HEADER.
//
// A .cpp is handed to the compiler directly. A .h is not compiled by itself -
// it only becomes code when something includes it - so a script written
// entirely in a header would never run. This file is what includes them.
fs::path GeneratedHeaderUnit() {
    return BuildDirectory() / "_headers.cpp";
}

// Is this text somewhere in the file? Used for two cheap checks that catch
// real mistakes: a file that declares a script but never registers it, and a
// header that is a script rather than a helper.
bool FileContains(const fs::path& path, std::string_view needle) {
    const std::string text = ReadWholeFile(path);
    return text.find(needle) != std::string::npos;
}

// Turns a real path back into the short form the rest of the engine uses, so
// what gets logged and recorded is "enemies/Chaser.cpp" rather than a path
// with somebody's user name in it. Falls back to the full path if the file is
// somehow outside the project.
std::string ToVirtualPath(const fs::path& real) {
    std::error_code ec;
    const fs::path  assetsRoot = fs::path(eng::FileSystem::Resolve(""));
    const fs::path  relative   = fs::relative(real, assetsRoot, ec);
    if (ec || relative.empty() || relative.native().starts_with(fs::path("..").native())) {
        return real.lexically_normal().generic_string();
    }
    return relative.generic_string();
}

// ---------------------------------------------------------------------------
//  Finding a compiler
// ---------------------------------------------------------------------------

#if defined(_WIN32)

// Asks vswhere where Visual Studio is. vswhere ships with every Visual Studio
// install since 2017 and always lives in the same place, which is what makes
// it the reliable way to find a compiler rather than guessing at paths.
std::string FindVisualStudioVcvars() {
    const char* programFiles = std::getenv("ProgramFiles(x86)");
    if (programFiles == nullptr) {
        programFiles = std::getenv("ProgramFiles");
    }
    if (programFiles == nullptr) {
        return {};
    }

    const fs::path vswhere =
        fs::path(programFiles) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";

    std::error_code ec;
    if (!fs::exists(vswhere, ec)) {
        return {};
    }

    const fs::path    logPath = fs::temp_directory_path() / "engine_vswhere.txt";
    const std::string command =
        "\"" + vswhere.string() + "\" -latest -products * " +
        "-requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 " +
        "-property installationPath";

    if (RunCaptured(command, logPath) != 0) {
        return {};
    }

    std::string installPath = ReadWholeFile(logPath);
    // Trim the trailing newline vswhere prints.
    while (!installPath.empty() &&
           (installPath.back() == '\n' || installPath.back() == '\r' ||
            installPath.back() == ' ')) {
        installPath.pop_back();
    }
    if (installPath.empty()) {
        return {};
    }

    const fs::path vcvars =
        fs::path(installPath) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat";
    return fs::exists(vcvars, ec) ? vcvars.string() : std::string{};
}

#endif // _WIN32

#if defined(_WIN32)

// Is cl.exe's environment set up, as opposed to cl.exe merely being findable?
//
// This distinction is the whole reason this function exists. MSVC does not
// know where its own standard headers are: it reads the INCLUDE environment
// variable to find <cmath>, and LIB to find the C runtime it links against.
// vcvars64.bat is what sets them. So a machine can have cl.exe sitting on the
// PATH and still be completely unable to compile anything - which is exactly
// what happens when the editor is launched from the Visual Studio IDE, because
// Visual Studio puts the toolchain's bin directory on the program's PATH
// without passing INCLUDE and LIB along with it.
//
// Getting this wrong produces a baffling error rather than an obvious one:
//
//     fatal error C1083: Cannot open include file: 'cmath'
//
// which reads like a broken compiler or a broken engine header, when in fact
// the compiler simply has not been told where its own headers live.
bool MsvcEnvironmentIsSetUp() {
    const char* include = std::getenv("INCLUDE");
    const char* lib     = std::getenv("LIB");
    return include != nullptr && *include != '\0' && lib != nullptr && *lib != '\0';
}

// ============================================================================
//  CALLING vcvars64.bat ONCE INSTEAD OF ONCE PER BUILD.
//
//  It costs just under SIX SECONDS on this machine - measured, not guessed -
//  and it was being paid by every single build script. On a build that
//  recompiles one file that was a third of the whole wait, spent entirely on
//  working out where the compiler keeps its headers.
//
//  So it is run once, its environment is copied into THIS process, and every
//  cl.exe started afterwards inherits it. That is exactly what a developer
//  command prompt is, arrived at from the other direction.
//
//  CACHED ON DISK, keyed on the batch file's own path and write time, so it is
//  once per Visual Studio install rather than once per editor session - which
//  matters for `editor --build-scripts`, a process that exists for one build
//  and would otherwise pay the six seconds every time.
//
//  AND VERIFIED AFTER LOADING THE CACHE. A Visual Studio update that leaves
//  the batch file's timestamp alone would leave a cache pointing at a toolset
//  that has moved, and the symptom is the baffling C1083 above. If the cached
//  environment does not look usable it is thrown away and vcvars is run again,
//  so the worst case is the old cost and never a broken build.
// ============================================================================

// The variables that matter. Everything vcvars sets is NOT copied: it also
// changes things like the prompt and a dozen VSCMD_ bookkeeping values, and
// quietly rewriting a running program's whole environment to compile a script
// is more than this needs to do.
const char* const kMsvcEnvironmentKeys[] = {
    "PATH",     "INCLUDE",           "LIB",            "LIBPATH",
    "VCINSTALLDIR", "VCToolsInstallDir", "VSINSTALLDIR",   "WindowsSdkDir",
    "WindowsSDKVersion", "UniversalCRTSdkDir", "UCRTVersion",
};

bool IsMsvcEnvironmentKey(std::string_view key) {
    for (const char* known : kMsvcEnvironmentKeys) {
        // Case-insensitively: Windows environment variable names are not
        // case-sensitive and `set` prints them however they were set.
        if (key.size() != std::strlen(known)) {
            continue;
        }
        bool same = true;
        for (std::size_t i = 0; i < key.size() && same; ++i) {
            same = std::tolower(static_cast<unsigned char>(key[i])) ==
                   std::tolower(static_cast<unsigned char>(known[i]));
        }
        if (same) {
            return true;
        }
    }
    return false;
}

fs::path MsvcEnvironmentCacheFile() { return BuildDirectory() / "msvcenv.txt"; }

// Applies KEY=VALUE lines. The first line is the cache's key and is skipped by
// the caller, not here.
void ApplyEnvironmentDump(const std::string& dump) {
    std::istringstream lines(dump);
    std::string        line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos || equals == 0) {
            continue;
        }
        const std::string key = line.substr(0, equals);
        if (!IsMsvcEnvironmentKey(key)) {
            continue;
        }
        _putenv_s(key.c_str(), line.substr(equals + 1).c_str());
    }
}

// What the cache has to match to be trusted.
std::string MsvcEnvironmentKeyLine(const std::string& vcvarsPath) {
    std::error_code ec;
    const auto      time = fs::last_write_time(fs::path(vcvarsPath), ec);
    const auto      ticks =
        ec ? 0LL : static_cast<long long>(time.time_since_epoch().count());
    return "vcvars=" + vcvarsPath + "|" + std::to_string(ticks);
}

bool AdoptMsvcEnvironment(const std::string& vcvarsPath) {
    if (vcvarsPath.empty()) {
        return false;
    }

    const std::string expected = MsvcEnvironmentKeyLine(vcvarsPath);
    const fs::path    cacheFile = MsvcEnvironmentCacheFile();

    // ---- the cheap path --------------------------------------------------
    {
        const std::string cached = ReadWholeFile(cacheFile);
        const std::size_t firstBreak = cached.find('\n');
        if (firstBreak != std::string::npos) {
            std::string header = cached.substr(0, firstBreak);
            if (!header.empty() && header.back() == '\r') {
                header.pop_back();
            }
            if (header == expected) {
                ApplyEnvironmentDump(cached.substr(firstBreak + 1));
                if (MsvcEnvironmentIsSetUp()) {
                    return true;
                }
                // The cache matched and is still not usable. Fall through and
                // ask vcvars, which is the self-healing half.
            }
        }
    }

    // ---- ask vcvars ------------------------------------------------------
    std::error_code ec;
    fs::create_directories(BuildDirectory(), ec);

    const fs::path batPath = BuildDirectory() / "msvcenv.bat";
    const fs::path dumpPath = BuildDirectory() / "msvcenv.dump";
    {
        std::ofstream bat(batPath, std::ios::trunc);
        bat << "@echo off\n";
        bat << "REM Written by the editor. It asks vcvars64.bat what it sets, once, so\n";
        bat << "REM that every later build does not have to run it again.\n";
        bat << "call \"" << vcvarsPath << "\" >nul 2>&1\n";
        bat << "set\n";
    }

    if (RunCaptured("\"" + batPath.string() + "\"", dumpPath) != 0) {
        return false;
    }

    const std::string dump = ReadWholeFile(dumpPath);
    ApplyEnvironmentDump(dump);
    if (!MsvcEnvironmentIsSetUp()) {
        return false;
    }

    // Only written once it is known to work, so a bad dump is never cached.
    {
        std::ofstream cache(cacheFile, std::ios::trunc);
        cache << expected << "\n" << dump;
    }
    fs::remove(dumpPath, ec);
    return true;
}

#endif // _WIN32

// Brings the compiler's environment into this process the first time a build
// needs it. Lazy rather than part of Init, so the editor's start-up does not
// pay for it on the first run after a Visual Studio install.
void EnsureCompilerEnvironment() {
#if defined(_WIN32)
    static bool tried = false;
    if (tried || g_vcvarsPath.empty()) {
        return;
    }
    tried = true;

    if (AdoptMsvcEnvironment(g_vcvarsPath)) {
        ENGINE_LOG_INFO(eng::Channels::kEditor,
                        "adopted the compiler's environment, so builds no longer run "
                        "vcvars64.bat (about six seconds each)");
        // Cleared so the generated build scripts stop calling it.
        g_vcvarsPath.clear();
    }
#endif
}

// Is this command available on the PATH?
bool IsOnPath(const std::string& program) {
#if defined(_WIN32)
    const std::string command = "where " + program;
#else
    const std::string command = "command -v " + program;
#endif
    const fs::path logPath = fs::temp_directory_path() / "engine_which.txt";
    return RunCaptured(command, logPath) == 0;
}

} // namespace

// ---------------------------------------------------------------------------
//  Init
// ---------------------------------------------------------------------------

void ScriptBuild::Init() {
    g_haveCompiler = false;
    g_compilerPath.clear();
    g_vcvarsPath.clear();
    g_compilerDescription.clear();

#if defined(_WIN32)
    // Best case: the editor was started from a developer prompt, so cl.exe
    // already works and needs no environment set up.
    //
    // Both halves of this condition matter. Finding cl.exe proves only that
    // the EXECUTABLE is reachable; MsvcEnvironmentIsSetUp proves the compiler
    // can find its own headers and libraries. Skipping the second check is how
    // the editor ends up running a compiler that cannot open <cmath>.
    if (IsOnPath("cl.exe") && MsvcEnvironmentIsSetUp()) {
        g_compilerPath        = "cl.exe";
        g_haveCompiler        = true;
        g_compilerDescription = "MSVC (cl.exe, already on PATH)";
    } else if (const std::string vcvars = FindVisualStudioVcvars(); !vcvars.empty()) {
        // Usual case: Visual Studio is installed but its compiler is not set
        // up for this process, so the build script runs vcvars64.bat first.
        // This branch is also where a half-configured environment lands - an
        // unconfigured cl.exe on the PATH is worse than no cl.exe at all, so
        // it is deliberately passed over in favour of vcvars.
        g_vcvarsPath          = vcvars;
        g_compilerPath        = "cl.exe";
        g_haveCompiler        = true;
        g_compilerDescription = "MSVC (via " + vcvars + ")";
    } else if (IsOnPath("clang++")) {
        g_compilerPath        = "clang++";
        g_haveCompiler        = true;
        g_compilerDescription = "clang++";
    }
#else
    for (const char* candidate : {"c++", "g++", "clang++"}) {
        if (IsOnPath(candidate)) {
            g_compilerPath        = candidate;
            g_haveCompiler        = true;
            g_compilerDescription = candidate;
            break;
        }
    }
#endif

    if (g_haveCompiler) {
        ENGINE_LOG_INFO(eng::Channels::kEditor, "scripts will be compiled with {}",
                        g_compilerDescription);
    } else {
        // Said plainly and once, at start-up, rather than being discovered the
        // first time somebody writes a script and nothing happens.
        //
        // The advice is picked BEFORE the log call rather than with a #if
        // inside it: a preprocessor directive cannot appear inside a macro's
        // argument list, and ENGINE_LOG_WARN is a macro.
#if defined(_WIN32)
        const char* advice = "Install Visual Studio, or the standalone Build Tools "
                             "for Visual Studio, and start the editor again.";
#else
        const char* advice = "Install g++ or clang++ and start the editor again.";
#endif
        ENGINE_LOG_WARN(eng::Channels::kEditor,
                        "no C++ compiler was found, so scripts cannot be built. {}",
                        advice);
    }
}

const std::string& ScriptBuild::CompilerDescription() { return g_compilerDescription; }
bool               ScriptBuild::HasCompiler()         { return g_haveCompiler; }
const std::string& ScriptBuild::LastOutput()          { return g_lastOutput; }

// ---------------------------------------------------------------------------
//  Deciding whether anything needs doing
// ---------------------------------------------------------------------------

std::vector<std::string> ScriptBuild::Sources::All() const {
    std::vector<std::string> all;
    all.reserve(compiled.size() + headers.size() + helpers.size());
    all.insert(all.end(), compiled.begin(), compiled.end());
    all.insert(all.end(), headers.begin(), headers.end());
    all.insert(all.end(), helpers.begin(), helpers.end());
    std::sort(all.begin(), all.end());
    return all;
}

ScriptBuild::Sources ScriptBuild::GatherSources() {
    Sources sources;

    const fs::path  root = fs::path(eng::FileSystem::Resolve(""));
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        return sources;   // no assets folder, which nothing can do anything about
    }

    // EVERY folder under assets/, however deep. That is the point: you arrange
    // your project however you like - enemies/, player/, ui/ - and the editor
    // finds the scripts wherever you put them, rather than making you keep
    // them all in one flat folder because the build only looks there.
    fs::recursive_directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        return sources;
    }

    for (const fs::directory_entry& entry : it) {
        // Skip hidden folders wholesale - .build, .git, and anything else
        // beginning with a dot is not somebody's game code.
        if (entry.is_directory(ec)) {
            const std::string name = entry.path().filename().string();
            if (!name.empty() && name.front() == '.') {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }

        const std::string ext = entry.path().extension().string();
        if (ext == ".cpp" || ext == ".cc" || ext == ".cxx") {
            sources.compiled.push_back(ToVirtualPath(entry.path()));
        } else if (ext == ".h" || ext == ".hpp" || ext == ".hxx") {
            // A header that registers a script is compiled through the
            // generated unit; every other header is only watched for changes.
            // See Sources.
            if (FileContains(entry.path(), "ENGINE_REGISTER_SCRIPT")) {
                sources.headers.push_back(ToVirtualPath(entry.path()));
            } else {
                sources.helpers.push_back(ToVirtualPath(entry.path()));
            }
        }
    }

    // Sorted so that the recorded list is comparable between runs rather than
    // depending on the order the operating system happened to hand them back.
    std::sort(sources.compiled.begin(), sources.compiled.end());
    std::sort(sources.headers.begin(), sources.headers.end());
    std::sort(sources.helpers.begin(), sources.helpers.end());
    return sources;
}

// ============================================================================
//  ONE LIBRARY PER SCRIPT
//
//  The editor compiles each script into its own .dll in .build/modules/, and a
//  build touches only the ones whose source actually changed. The engine loads
//  the whole folder - see ScriptLibrary.h.
//
//  TWO THINGS THIS BUYS, and the second matters more than the first:
//
//    SPEED. Editing one script compiles one file and links one small library.
//    The old single-library build recompiled all eleven every time, because
//    one library is one link and one link needs every object.
//
//    A BROKEN SCRIPT STOPS BEING EVERYONE'S PROBLEM. With one library, a typo
//    in one file meant nothing linked, so nothing was produced, so NO script
//    ran. Now the failure costs exactly the script it is in.
//
//  AN EXPORT STILL GETS ONE userContent.dll. A shipped game has no reason to
//  carry eleven libraries, and nothing is ever going to recompile one of them.
//  That is the same objects linked a different way - see BuildBundle - so
//  exporting after a build is a link, not a rebuild.
//
//  ==========================================================================
//  EVERY SOURCE IS COMPILED THROUGH A GENERATED UNIT THAT #includes IT.
//
//  Not copied - included. Line numbers, __FILE__ and the file's own anonymous
//  namespace all stay exactly as they were, so a compiler error still points
//  at the line you wrote. ScriptVars.h makes the same point about the exposer
//  it generates, and this is the same trick applied to every file.
//
//  The reason EVERY file gets one, even a helper with nothing to expose, is
//  duller: the units are compiled in a single `cl /c /MP` so that one process
//  does all of them in parallel, and /Fo names each object after its source.
//  Two scripts called Player.cpp in different folders would collide. A unit
//  named after the whole virtual path cannot.
// ============================================================================

fs::path ModuleDirectory() { return BuildDirectory() / "modules"; }
fs::path UnitDirectory()   { return BuildDirectory() / "units"; }
fs::path ObjectDirectory() { return BuildDirectory() / "obj"; }

// The platform's extension for a loadable library. Must agree with
// ScriptLibrary's, which is what lists the folder looking for them.
const char* LibrarySuffix() {
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

// FNV-1a. Short, stable across runs and platforms, and good enough to tell two
// paths apart - which is all that is being asked of it. Nothing security-ish
// depends on this.
std::string ShortHash(std::string_view text) {
    std::uint32_t hash = 2166136261u;
    for (const unsigned char c : text) {
        hash ^= c;
        hash *= 16777619u;
    }
    char out[16];
    std::snprintf(out, sizeof(out), "%08x", hash);
    return out;
}

// "scripts/enemies/Chaser.cpp" -> "Chaser__1a2b3c4d"
//
// The STEM so that what lands in .build/ and in an error message is readable,
// and the HASH so that two files with the same name in different folders get
// different libraries. The project has `Sprite Animator/Sprite_Animator.cpp`
// already; a second `Sprite_Animator.cpp` elsewhere must not overwrite it.
std::string ModuleBaseName(const std::string& virtualPath) {
    std::string stem = fs::path(virtualPath).stem().string();
    for (char& c : stem) {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '_';
        if (!safe) {
            c = '_';
        }
    }
    if (stem.empty()) {
        stem = "script";
    }
    return stem + "__" + ShortHash(virtualPath);
}

// One source file on its way to being compiled.
struct BuildUnit {
    std::string virtualSource;   // "scripts/Orbiter.cpp", for messages
    fs::path    realSource;
    fs::path    unitPath;        // .build/units/<base>.cpp
    fs::path    objectPath;      // .build/obj/<base>.obj
    fs::path    libraryPath;     // .build/modules/<base>.dll; empty for a helper

    // Empty for a .cpp that registers no script. Those are compiled once and
    // linked into EVERY script library, because a script calling a function
    // from one has to find it - with a single library that happened for free.
    std::string className;

    bool isScript = false;

    // Set when this unit HAD a library and PlanBuild threw it away because the
    // configuration changed - see the deletion in PlanBuild.
    //
    // RECORDED RATHER THAN WORKED OUT AFTERWARDS. The failure message needs
    // to tell a script that was discarded apart from one that was never built,
    // and after the file has gone those two look identical. Inferring it from
    // "the configuration changed" is wrong whenever the configuration changed
    // AND there was nothing there - which is the second build in a row that
    // fails, a case reached immediately in practice.
    bool discardedLibrary = false;

    // `stale` is about the OBJECT: the source, or something every file depends
    // on, is newer than it. `needsLink` is about the LIBRARY, and is a
    // separate question - a library can be missing or stale while its object
    // is perfectly current, which is exactly what happens when an export has
    // just pruned the modules folder.
    bool stale     = false;
    bool needsLink = false;
};

// The write time of a file, or `fallback` when it is not there. Used so that a
// missing file reads as "infinitely old" or "infinitely new" depending on
// which answer makes the caller rebuild.
fs::file_time_type FileTimeOr(const fs::path& path, fs::file_time_type fallback) {
    std::error_code ec;
    const auto      time = fs::last_write_time(path, ec);
    return ec ? fallback : time;
}

// The newest of a set of virtual paths.
fs::file_time_type NewestOf(const std::vector<std::string>& virtualPaths,
                            fs::file_time_type start) {
    fs::file_time_type newest = start;
    for (const std::string& path : virtualPaths) {
        const auto time =
            FileTimeOr(fs::path(eng::FileSystem::Resolve(path)), fs::file_time_type::min());
        newest = std::max(newest, time);
    }
    return newest;
}

// ============================================================================
//  WHAT THE SCRIPTS LOOKED LIKE WHEN A BUILD LAST FAILED.
//
//  A failed build leaves its inputs exactly as they were, and it deleted the
//  objects it was about to make - so every check that follows says "still out
//  of date", and says it forever. The editor then tries again on every alt-tab
//  back, and each attempt pushes the compiler's explanation of the FIRST one
//  further up the Console. The error you need to read is the thing the retries
//  bury.
//
//  So a failure is remembered, and nothing is tried again until one of the
//  files the build reads actually changes. Fix the script and it rebuilds on
//  the next focus, exactly as it did before.
//
//  WHAT COUNTS AS A CHANGE: any script or helper header edited, added or
//  removed, and switching configuration. All of those are in the fingerprint
//  below, which is why NeedsRebuild can test it before anything else - no
//  change to the inputs means no new answer is possible.
//
//  DELIBERATELY NOT WRITTEN TO DISK. A record that outlived the editor could
//  suppress a build that was genuinely needed, which is the one failure this
//  file takes the most trouble to avoid, and the only way out would be to
//  delete a file by hand. Kept in memory, restarting the editor is always an
//  escape hatch - and so is Rebuild in the Assets panel, which calls
//  BeginAsync directly and never asks this question.
//
//  MAIN THREAD ONLY. FinishBuild writes it and NeedsRebuild reads it, and both
//  are main-thread by contract. Compile is the part that runs on a worker, and
//  it does not touch this.
// ============================================================================
std::string g_failedInputs;

// Everything a build reads, and when. Mtime AND size, because a one-character
// fix is exactly the edit somebody makes after reading a compiler error, and
// two saves inside the same filesystem tick are not far-fetched.
std::string InputFingerprint(const ScriptBuild::Sources& sources) {
    std::ostringstream out;
    out << CurrentBuildStamp() << '\n';
    for (const std::string& source : sources.All()) {
        const fs::path  real = fs::path(eng::FileSystem::Resolve(source));
        std::error_code ec;
        const auto      size = fs::file_size(real, ec);
        out << source << '\t'
            << FileTimeOr(real, fs::file_time_type::min()).time_since_epoch().count()
            << '\t' << (ec ? 0LL : static_cast<long long>(size)) << '\n';
    }
    return out.str();
}

// Remembers a failure so it is not retried until something changes, and
// forgets one after a success. See g_failedInputs.
void RememberBuildOutcome(const ScriptBuild::Result&  result,
                          const ScriptBuild::Sources& sources) {
    if (result.ok) {
        g_failedInputs.clear();
        return;
    }

    g_failedInputs = InputFingerprint(sources);

    // SAID ONCE, which is the whole point - this line replaces the repeated
    // attempts rather than joining them. At Error because config/engine.json
    // sets the threshold to Error in this project, and an explanation of why
    // the editor has stopped trying has to survive that. See the note on the
    // export messages in EditorApp::Run.
    ENGINE_LOG_ERROR(eng::Channels::kEditor,
                     "not building again until a script changes - fix the problem above, "
                     "or press Rebuild in the Assets panel to try the same files again");
}

// ============================================================================
//  A PRECOMPILED HEADER, which is where the time actually goes.
//
//  Measured on this machine, compiling one script was about NINE AND A HALF
//  SECONDS and linking its library was THIRTEEN MILLISECONDS. Almost none of
//  that is the script: <engine/Engine.h> pulls in the whole engine, SDL and
//  nlohmann's JSON, and every script pays to parse all of it.
//
//  So it is parsed once into .build/obj/engine.pch and every script is
//  compiled against that instead.
//
//  THE GENERATED UNIT INCLUDES Engine.h AS ITS FIRST LINE, whatever the script
//  itself includes. /Yu skips the translation unit up to and including that
//  header and substitutes the precompiled one, so it has to be there and it
//  has to be first - and because the unit is generated, that is guaranteed
//  rather than hoped for. A script including its own selection of engine
//  headers still works: the include guards make them no-ops.
//
//  WINDOWS ONLY. GCC and Clang precompile headers differently enough to need
//  their own handling, and the POSIX path here has always been the simpler
//  fallback. It is correct there, just not as fast.
// ============================================================================

// The header as the generated unit spells it. /Yc and /Yu must both be given
// exactly this text, and it must match the #include character for character.
const char* PchHeaderName() { return "engine/Engine.h"; }

fs::path PchStubPath()   { return BuildDirectory() / "pch.cpp"; }
fs::path PchFilePath()   { return ObjectDirectory() / "engine.pch"; }
fs::path PchObjectPath() { return ObjectDirectory() / "pch.obj"; }

struct BuildPlan {
    std::vector<BuildUnit> units;
    std::vector<fs::path>  helperObjects;   // linked into every script library
    bool                   anyToCompile = false;
    bool                   anyToLink    = false;

    // The precompiled header has to be made before anything can be compiled
    // against it, and remade whenever an engine header changes.
    bool usePch  = false;
    bool pchStale = false;
};

// Writes the one-line unit that wraps a source file. See the banner above for
// why everything goes through one of these.
// The first line of every generated unit: the precompiled header, so that /Yu
// has something to match. See the banner above for why it is here and not left
// to the script.
void WritePchInclude(std::ostream& out) {
    out << "#include <" << PchHeaderName()
        << ">   // the precompiled header - see ScriptBuild.cpp\n";
}

void WritePlainUnit(const BuildUnit& unit) {
    std::ofstream out(unit.unitPath, std::ios::trunc);
    WritePchInclude(out);
    out << "// Written by the editor. Do not edit, and do not commit.\n";
    out << "//\n";
    out << "// It INCLUDES the source rather than copying it, so every line number\n";
    out << "// and every __FILE__ stays exactly as it was - a compiler error still\n";
    out << "// points at the line you wrote. See ScriptBuild.cpp.\n";
    // FORWARD SLASHES: a backslash inside an #include is an escape sequence as
    // far as the preprocessor is concerned.
    out << "#include \"" << unit.realSource.generic_string() << "\"\n";
}

// Deletes anything in a generated folder that is not part of this build.
//
// WITHOUT THIS A DELETED SCRIPT KEEPS RUNNING. Its library would still be
// sitting in modules/, and the engine loads the whole folder - so removing a
// script from the project would change nothing until somebody noticed the
// orphan by hand.
void PruneStrays(const fs::path& directory, std::string_view extension,
                 const std::vector<std::string>& keepStems) {
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != extension) {
            continue;
        }
        const std::string stem = entry.path().stem().string();
        if (std::find(keepStems.begin(), keepStems.end(), stem) == keepStems.end()) {
            fs::remove(entry.path(), ec);
        }
    }
}

// Works out what to compile and what to link, from what is on disk.
//
// `forceAll` is for the cases a timestamp cannot see - a different build
// configuration, or a source list that has changed shape.
// `dryRun` asks the same question without touching anything, which is what
// NeedsRebuild needs - it runs whenever the editor regains focus, and a check
// that rewrote the generated units every time would be a check with opinions.
// Sharing the predicate is the point: two copies of "is this out of date"
// would disagree eventually, and the symptom is a script that never rebuilds.
BuildPlan PlanBuild(const ScriptBuild::Sources& sources, bool forceAll,
                    bool dryRun = false) {
    BuildPlan       plan;
    std::error_code ec;

    if (!dryRun) {
        fs::create_directories(UnitDirectory(), ec);
        fs::create_directories(ObjectDirectory(), ec);
        fs::create_directories(ModuleDirectory(), ec);

        // The previous layout's folder. Left behind it is just something to
        // be misled by, and .build/ is a folder people are told they can read.
        fs::remove_all(BuildDirectory() / "expose", ec);
    }

    // ---- one unit per .cpp, scripts and helpers alike ---------------------
    std::vector<std::string> supportSources;

    for (const std::string& source : sources.compiled) {
        BuildUnit unit;
        unit.virtualSource = source;
        unit.realSource    = fs::path(eng::FileSystem::Resolve(source));

        const std::string base = ModuleBaseName(source);
        unit.unitPath          = UnitDirectory() / (base + ".cpp");
        unit.objectPath        = ObjectDirectory() / (base + ".obj");

        const eng::ScriptSourceInfo info = eng::ScanScriptSource(ReadWholeFile(unit.realSource));
        unit.className                   = info.className;
        unit.isScript                    = !info.className.empty();

        if (unit.isScript) {
            unit.libraryPath = ModuleDirectory() / (base + LibrarySuffix());
        } else {
            supportSources.push_back(source);
            plan.helperObjects.push_back(unit.objectPath);
        }

        // ---- the unit, written every time ---------------------------------
        //
        // Cheap, and it keeps the generated exposer in step with the source
        // without a second staleness rule to get wrong. The unit's own write
        // time is never compared against anything; the SOURCE's is.
        if (dryRun) {
            // Nothing is written, and nothing is logged: this runs on every
            // focus change and would otherwise narrate itself constantly.
        } else if (unit.isScript && !info.hasOnExpose && !info.vars.empty()) {
            for (const eng::ScriptVarSkipped& skipped : info.skipped) {
                ENGINE_LOG_WARN(eng::Channels::kEditor,
                                "{}: '{}' is not shown in the Inspector because {}",
                                source, skipped.name, skipped.reason);
            }

            std::ofstream out(unit.unitPath, std::ios::trunc);
            if (out) {
                // The precompiled header first; GenerateExposerUnit knows
                // nothing about build strategy and should not have to.
                WritePchInclude(out);
                out << eng::GenerateExposerUnit(info, unit.realSource.generic_string());
            } else {
                ENGINE_LOG_ERROR(eng::Channels::kEditor,
                                 "could not write '{}', so '{}' will have no variables "
                                 "in the Inspector",
                                 unit.unitPath.string(), info.className);
                WritePlainUnit(unit);
            }

            std::string names;
            for (const eng::ScriptVarDecl& var : info.vars) {
                names += names.empty() ? "" : ", ";
                names += var.name;
            }
            ENGINE_LOG_INFO(eng::Channels::kEditor, "{}: exposing {}", source, names);
        } else {
            WritePlainUnit(unit);
        }

        plan.units.push_back(std::move(unit));
    }

    // ---- the scripts written in HEADERS, which share one library ---------
    //
    // A .h is not compiled on its own, so a script written entirely in one
    // would never run. They are gathered into a single generated unit, exactly
    // as before - there is no per-script granularity to be had here, because
    // there is no per-script translation unit to begin with.
    if (!sources.headers.empty()) {
        BuildUnit unit;
        unit.virtualSource = "scripts written in headers";
        unit.realSource    = GeneratedHeaderUnit();
        unit.unitPath      = GeneratedHeaderUnit();
        unit.objectPath    = ObjectDirectory() / "_headers.obj";
        unit.libraryPath   = ModuleDirectory() / (std::string("_headers") + LibrarySuffix());
        unit.isScript      = true;

        if (!dryRun) {
            std::ofstream out(unit.unitPath, std::ios::trunc);
            WritePchInclude(out);
            out << "// Written by the editor. Do not edit - it is regenerated on every\n";
            out << "// build from the .h files under assets/ that register a script.\n";
            for (const std::string& header : sources.headers) {
                out << "#include \""
                    << fs::path(eng::FileSystem::Resolve(header)).generic_string()
                    << "\"\n";
            }
        }

        plan.units.push_back(std::move(unit));
    } else if (!dryRun) {
        fs::remove(GeneratedHeaderUnit(), ec);
    }

    // ---- what invalidates EVERYTHING -------------------------------------
    //
    // An engine header, a project header, or any helper .cpp - because a
    // helper's object is linked into every script library, so a change to one
    // means every library has to be made again.
    //
    // Conservative on purpose. Tracking which script includes which header
    // would need the compiler's own dependency output, and being wrong about
    // it means running stale code while the Console says the build succeeded.
    fs::path           newestHeader;
    fs::file_time_type engineHeaderTime = fs::file_time_type::min();
    NewestEngineHeader(newestHeader, engineHeaderTime);

    fs::file_time_type globalTime = engineHeaderTime;
    globalTime = NewestOf(sources.helpers, globalTime);
    globalTime = NewestOf(sources.headers, globalTime);
    globalTime = NewestOf(supportSources, globalTime);

    // ---- the precompiled header ------------------------------------------
#if defined(_WIN32)
    plan.usePch = true;

    const auto pchTime = FileTimeOr(PchFilePath(), fs::file_time_type::min());
    plan.pchStale      = forceAll || !fs::exists(PchFilePath(), ec) ||
                    !fs::exists(PchObjectPath(), ec) || engineHeaderTime > pchTime;

    if (plan.usePch && !dryRun) {
        // Rewritten every time: it is one line, and a stub that has drifted
        // from the header name in /Yc is a baffling failure rather than an
        // obvious one.
        std::ofstream stub(PchStubPath(), std::ios::trunc);
        stub << "// Written by the editor. Do not edit.\n";
        stub << "//\n";
        stub << "// Compiling this one file is what produces engine.pch, which every\n";
        stub << "// script is then compiled against. See ScriptBuild.cpp.\n";
        stub << "#include <" << PchHeaderName() << ">\n";
    }

    // A rebuilt precompiled header means every object has to be made again:
    // the engine header that invalidated it invalidates them too, and
    // globalTime already says so. Asserting the link here would be the same
    // statement twice.
    plan.anyToCompile = plan.anyToCompile || plan.pchStale;
#endif

    // ---- and so what is actually out of date -----------------------------
    for (BuildUnit& unit : plan.units) {
        const auto sourceTime = FileTimeOr(unit.realSource, fs::file_time_type::max());
        const auto objectTime = FileTimeOr(unit.objectPath, fs::file_time_type::min());

        unit.stale = forceAll || !fs::exists(unit.objectPath, ec) ||
                     sourceTime > objectTime || globalTime > objectTime;

        if (unit.isScript) {
            const auto libraryTime = FileTimeOr(unit.libraryPath, fs::file_time_type::min());
            unit.needsLink = unit.stale || !fs::exists(unit.libraryPath, ec) ||
                             objectTime > libraryTime || globalTime > libraryTime;
        }

        plan.anyToCompile = plan.anyToCompile || unit.stale;
        plan.anyToLink    = plan.anyToLink || unit.needsLink;

        // DELETED BEFORE COMPILING, so that "the object exists" means "it
        // compiled THIS TIME". The batch links a library only when its object
        // is there, which is how one script's failure leaves the others alone;
        // a leftover object from the last build would defeat that and link
        // yesterday's code over today's error.
        if (unit.stale && !dryRun) {
            fs::remove(unit.objectPath, ec);

            // ================================================================
            //  AND THE LIBRARY TOO, BUT ONLY WHEN THE CONFIGURATION CHANGED.
            //
            //  Normally the library is kept on purpose. That is what "the
            //  version from the last successful build is still what runs"
            //  means: a script with a syntax error goes on working while you
            //  fix it, which is the whole value of one library per script.
            //
            //  `forceAll` means the build STAMP changed - a different C
            //  runtime, a different engine build, a different script ABI. In
            //  that case the kept library was built for a DIFFERENT ENGINE,
            //  and keeping it is not kindness but the exact heap-corruption
            //  case this project goes to some trouble to prevent. See
            //  engine/include/engine/project/ScriptAbi.h.
            //
            //  HOW IT BITES, because it needs two things to go wrong at once
            //  and so looks unreachable: switch from Release to Debug, which
            //  recompiles everything, and have ONE script fail to compile.
            //  Its object is gone, no new library is linked, and the old
            //  Release-built one is still sitting there - so the modules
            //  folder holds ten Debug libraries and one Release library, and
            //  the editor loads all eleven. Observed, before this line
            //  existed.
            //
            //  Deleting it means that script shows as NOT FOUND until it
            //  compiles, which is honest: there genuinely is no usable build
            //  of it for this engine.
            // ================================================================
            if (forceAll && unit.isScript && fs::exists(unit.libraryPath, ec)) {
                fs::remove(unit.libraryPath, ec);
                unit.discardedLibrary = true;
            }
        }
    }

    // ---- clear out what is no longer part of the build -------------------
    std::vector<std::string> stems;
    for (const BuildUnit& unit : plan.units) {
        stems.push_back(unit.objectPath.stem().string());
    }

    // THE PRECOMPILED HEADER'S OWN OBJECT IS NOT A UNIT, and leaving it out of
    // this list deleted it on the build after the one that made it - which
    // showed up as every link failing on "cannot open input file pch.obj".
    // Found by running a second build, which is the only thing that would have
    // found it.
    stems.push_back(PchObjectPath().stem().string());
    stems.push_back("userContent");   // the bundle's import library
    if (dryRun) {
        return plan;
    }

    PruneStrays(UnitDirectory(), ".cpp", stems);
    PruneStrays(ObjectDirectory(), ".obj", stems);
    PruneStrays(ObjectDirectory(), ".lib", stems);
    PruneStrays(ObjectDirectory(), ".exp", stems);
    PruneStrays(ModuleDirectory(), LibrarySuffix(), stems);

    return plan;
}

bool ScriptBuild::NeedsRebuild() {
    const Sources sources = GatherSources();

    if (sources.Empty()) {
        // Nothing to build. A modules folder left over from scripts that have
        // all since been deleted does need clearing away, and the source-list
        // check is what notices: it recorded names that no longer exist.
        return !ReadWholeFile(SourceListFile()).empty();
    }

    // HAS A FAILED BUILD ALREADY TRIED THESE EXACT FILES?
    //
    // Asked before anything else, because everything below answers from the
    // OUTPUTS - and a failed build deleted the objects it was about to make,
    // so all of them report "out of date" and will go on reporting it with
    // nothing left that could change the answer. Retrying on every alt-tab
    // buries the compiler's explanation under copies of itself.
    //
    // The fingerprint covers the configuration and the whole source set, so
    // the two checks below cannot be skipped by this: anything they would
    // notice changes the fingerprint first. See g_failedInputs.
    if (!g_failedInputs.empty() && InputFingerprint(sources) == g_failedInputs) {
        return false;
    }

    // WAS IT BUILT FOR THIS CONFIGURATION?
    //
    // Checked before anything else, because this is the case timestamps cannot
    // see: switching between the Debug and Release presets changes nothing
    // about the scripts themselves, so every source is still older than every
    // object and nothing looks out of date - while the objects are now
    // unusable. See CurrentBuildStamp.
    if (CurrentBuildStamp() != ReadWholeFile(BuildStampFile())) {
        return true;
    }

    // Did the SET of scripts change? This is what notices a deleted or renamed
    // file, which no timestamp comparison can.
    std::string current;
    for (const std::string& source : sources.All()) {
        current += source + "\n";
    }
    if (current != ReadWholeFile(SourceListFile())) {
        return true;
    }

    // And then the same question the build itself asks, asked the same way.
    //
    // ONE PREDICATE, shared. Two copies of "is this out of date" drift apart,
    // and the symptom is the worst kind: a script that is never rebuilt
    // because this said no while the build would have said yes.
    const BuildPlan plan = PlanBuild(sources, false, /*dryRun=*/true);
    return plan.anyToCompile || plan.anyToLink;
}

// ---------------------------------------------------------------------------
//  Building
// ---------------------------------------------------------------------------

// ============================================================================
//  A BUILD HAPPENS IN THREE PHASES, AND ONLY THE MIDDLE ONE IS SAFE OFF THE
//  MAIN THREAD.
//
//  This split exists because compiling on a worker thread without it is
//  quietly disastrous. Unloading the script library DESTROYS every live
//  script object, and loading it recreates and rebinds them - both while the
//  main thread may be half way through ticking that same list. The compile
//  itself touches nothing but files and a compiler process, so that is the
//  part that can move.
//
//    BeginBuild()  main thread   decides, and unloads
//    Compile()     WORKER        writes the build script, runs the compiler
//    FinishBuild() main thread   loads the result and rebinds
//
//  BuildAndReload() runs all three in order and is what the synchronous
//  callers still use - `--build-scripts`, and anything that wants to wait.
// ============================================================================

bool ScriptBuild::BeginBuild(Result& out) {
    out = Result{};

    const Sources   sources  = GatherSources();
    std::error_code ec;
    fs::create_directories(BuildDirectory(), ec);

    // ---- nothing to compile ----------------------------------------------
    if (sources.Empty()) {
        eng::ScriptLibrary::Unload();

        // EVERY generated thing goes, not just the bundle. A modules folder
        // left behind would be loaded on the next start, so deleting the last
        // script in a project would appear to change nothing at all.
        fs::remove(OutputLibrary(), ec);
        fs::remove_all(ModuleDirectory(), ec);
        fs::remove_all(ObjectDirectory(), ec);
        fs::remove_all(UnitDirectory(), ec);

        std::ofstream(SourceListFile(), std::ios::trunc);      // record: nothing built
        std::ofstream(ClassManifestFile(), std::ios::trunc);

        out.rebuilt = true;
        out.summary = "no scripts found in assets/ - nothing to build";
        ENGINE_LOG_INFO(eng::Channels::kEditor, "{}", out.summary);
        return false;   // already finished; there is nothing to compile
    }

    if (!g_haveCompiler) {
        out.ok      = false;
        out.summary = "scripts changed, but there is no C++ compiler to build them";
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "{} ({})", out.summary,
                         "see the message at start-up for what to install");
        return false;
    }

    // ---- unload BEFORE building ------------------------------------------
    //
    // Two reasons, and both are hard failures rather than untidiness. Windows
    // refuses to overwrite a file that is currently loaded, so the link step
    // would simply fail. And every script object in the running scene was
    // created by code inside this library; destroying them first is the only
    // way they can be destroyed at all.
    //
    // ON THE MAIN THREAD, always. This is the line that cannot move to a
    // worker: it runs destructors for objects the main thread is holding.
    eng::ScriptLibrary::Unload();
    return true;
}

ScriptBuild::Result ScriptBuild::Compile() {
    Result result;

    // The compiler-environment adoption, which happens once per process and
    // takes about six seconds off every build after the first. See
    // AdoptMsvcEnvironment.
    EnsureCompilerEnvironment();

    // Re-gathered rather than passed in, so this function depends on nothing
    // the main thread is still holding. Walking assets/ costs nothing next to
    // running a compiler.
    const Sources                  sources    = GatherSources();
    const std::vector<std::string> allSources = sources.All();

    std::error_code ec;
    const fs::path  buildDir = BuildDirectory();
    fs::create_directories(buildDir, ec);

    // ---- what actually needs doing ---------------------------------------
    //
    // The stamp is tested here as well as in NeedsRebuild, because this is the
    // one place that can act on it: a change of configuration invalidates
    // every object and no timestamp can see it. See CurrentBuildStamp.
    const bool stampChanged = CurrentBuildStamp() != ReadWholeFile(BuildStampFile());

    const BuildPlan             plan      = PlanBuild(sources, stampChanged);
    const fs::path              scriptLog = buildDir / "build.log";
    const fs::path              engineLib = EngineLinkLibrary();
    const std::vector<fs::path> includes  = IncludeDirectories();

    if (!plan.anyToCompile && !plan.anyToLink) {
        // The caller still reloads: BeginBuild has already unloaded, so
        // returning here with nothing loaded would leave every script
        // showing as NOT FOUND. See the phase note above.
        result.ok      = true;
        result.rebuilt = false;
        result.summary = "scripts are already up to date";
        return result;
    }

    std::size_t compiling = 0;
    std::size_t linking   = 0;
    for (const BuildUnit& unit : plan.units) {
        compiling += unit.stale ? 1 : 0;
        linking += unit.needsLink ? 1 : 0;
    }
    ENGINE_LOG_INFO(eng::Channels::kEditor,
                    "compiling {} of {} file(s), linking {} librar{}", compiling,
                    plan.units.size(), linking, linking == 1 ? "y" : "ies");

    std::ostringstream cmd;

#if defined(_WIN32)
    const fs::path batPath = buildDir / "build.bat";
    {
        std::ofstream bat(batPath, std::ios::trunc);
        bat << "@echo off\n";
        bat << "REM Written by the editor. Safe to read, and safe to run by hand in a\n";
        bat << "REM terminal if you want to see exactly what the compiler is doing.\n";
        if (!g_vcvarsPath.empty()) {
            // Sets up the environment cl.exe needs - where its own headers and
            // libraries are. Without it cl reports that it cannot find
            // <cstddef>, which looks like a broken compiler and is not one.
            //
            // ONCE, for the whole batch. It is the slowest thing in here, so a
            // build that compiles one file pays for it once rather than once
            // per file - which is most of the reason the compiles and the
            // links share a single script.
            bat << "call \"" << g_vcvarsPath << "\" >nul 2>&1\n";
        }

        // ---- the precompiled header, before anything uses it -------------
        //
        // NOT with /MP. One file does not need splitting across processes, and
        // /Yc with /MP is not a combination MSVC accepts.
        if (plan.usePch && plan.pchStale) {
            bat << "cl.exe /nologo /c /std:c++20 /EHsc";
            bat << (ENGINE_SCRIPT_IS_DEBUG ? " /MDd" : " /MD");
            bat << " /W3";
            for (const fs::path& include : includes) {
                bat << " /I\"" << include.string() << "\"";
            }
            bat << " /Yc\"" << PchHeaderName() << "\"";
            bat << " /Fp\"" << PchFilePath().string() << "\"";
            bat << " /Fo\"" << PchObjectPath().string() << "\"";
            bat << " \"" << PchStubPath().string() << "\"\n";
        }

        // ---- one compile step for everything out of date -----------------
        //
        // /MP hands the files to as many processes as there are cores. One
        // invocation with eleven files is what makes a cold build parallel;
        // eleven invocations would each pay for cl's own start-up.
        if (plan.anyToCompile) {
            bat << "cl.exe /nologo /c /MP /std:c++20 /EHsc";
            // The C runtime setting has to match the engine's - see
            // ScriptBuild.h.
            bat << (ENGINE_SCRIPT_IS_DEBUG ? " /MDd" : " /MD");
            // Warnings are kept modest on purpose: this is somebody's game
            // code, not the engine, and an unused parameter in a half-written
            // script should not produce a wall of output.
            bat << " /W3";
            for (const fs::path& include : includes) {
                bat << " /I\"" << include.string() << "\"";
            }
            // EVERY UNIT IS COMPILED AGAINST THE PRECOMPILED HEADER, with the
            // same flags /Yc was given. A mismatch is refused by the compiler
            // rather than quietly ignored, which is the good outcome.
            if (plan.usePch) {
                bat << " /Yu\"" << PchHeaderName() << "\"";
                bat << " /Fp\"" << PchFilePath().string() << "\"";
            }
            bat << " /Fo\"" << ObjectDirectory().string() << "\\\\\"";
            for (const BuildUnit& unit : plan.units) {
                if (unit.stale) {
                    bat << " \"" << unit.unitPath.string() << "\"";
                }
            }
            bat << "\n";
        }

        // ---- one link step per script -------------------------------------
        //
        // GUARDED ON THE OBJECT EXISTING, and that guard is the whole feature.
        // PlanBuild deleted the object of everything it was about to compile,
        // so `if exist` means "this one compiled just now". A script that did
        // not compile is simply not linked - its library is left exactly as it
        // was, and every other script still builds and still runs.
        for (const BuildUnit& unit : plan.units) {
            if (!unit.needsLink) {
                continue;
            }
            bat << "if exist \"" << unit.objectPath.string() << "\" ";
            bat << "cl.exe /nologo /LD";
            bat << (ENGINE_SCRIPT_IS_DEBUG ? " /MDd" : " /MD");
            bat << " \"" << unit.objectPath.string() << "\"";
            // The precompiled header's own object. /Yc produces real code as
            // well as the .pch, and a library linked without it is missing
            // whatever Engine.h defined out of line.
            if (plan.usePch) {
                bat << " \"" << PchObjectPath().string() << "\"";
            }
            // Every helper .cpp goes into every script library. With one
            // library that happened for free; separate libraries have to be
            // told, or a script calling a helper's function would not link.
            for (const fs::path& helper : plan.helperObjects) {
                if (helper != unit.objectPath) {
                    bat << " \"" << helper.string() << "\"";
                }
            }
            bat << " /Fe\"" << unit.libraryPath.string() << "\"";
            bat << " /link \"" << engineLib.string() << "\"";
            // The import library and its exports table are build droppings.
            // Sent to obj/ so that modules/ holds libraries and nothing else -
            // the engine lists that folder and tries to load what it finds.
            bat << " /IMPLIB:\"" << (ObjectDirectory() / (unit.objectPath.stem().string() + ".lib")).string() << "\"";
            bat << "\n";
        }
    }
    cmd << "\"" << batPath.string() << "\"";
#else
    const fs::path shPath = buildDir / "build.sh";
    {
        std::ofstream sh(shPath, std::ios::trunc);
        sh << "#!/bin/sh\n";
        sh << "# Written by the editor. Safe to read, and safe to run by hand.\n";

        // No /MP equivalent that is worth the complication here; these run one
        // after another. The incremental part is what matters, and it is the
        // same on every platform.
        for (const BuildUnit& unit : plan.units) {
            if (!unit.stale) {
                continue;
            }
            sh << g_compilerPath << " -std=c++20 -fPIC -Wall -c";
            for (const fs::path& include : includes) {
                sh << " -I\"" << include.string() << "\"";
            }
            sh << " -o \"" << unit.objectPath.string() << "\"";
            sh << " \"" << unit.unitPath.string() << "\"\n";
        }

        for (const BuildUnit& unit : plan.units) {
            if (!unit.needsLink) {
                continue;
            }
            // Same guard as the Windows branch, and for the same reason.
            sh << "[ -f \"" << unit.objectPath.string() << "\" ] && ";
            sh << g_compilerPath << " -shared -o \"" << unit.libraryPath.string() << "\"";
            sh << " \"" << unit.objectPath.string() << "\"";
            for (const fs::path& helper : plan.helperObjects) {
                if (helper != unit.objectPath) {
                    sh << " \"" << helper.string() << "\"";
                }
            }
            sh << " \"" << engineLib.string() << "\"\n";
        }
    }
    cmd << "sh \"" << shPath.string() << "\"";
#endif

    // ---- run it -----------------------------------------------------------
    const int exitCode = RunCaptured(cmd.str(), scriptLog);
    g_lastOutput       = ReadWholeFile(scriptLog);
    result.output      = g_lastOutput;
    result.rebuilt     = true;

    // ======================================================================
    //  WHICH FILES ACTUALLY FAILED, asked of the FILESYSTEM rather than of
    //  the exit code.
    //
    //  One cl invocation compiles everything, so its exit code says only
    //  "something went wrong" - it cannot say which script, and with separate
    //  libraries that is the only interesting question. The objects and
    //  libraries on disk can: PlanBuild deleted the ones it was about to
    //  rebuild, so anything still missing is something that did not get made.
    // ======================================================================
    std::vector<std::string> failed;
    std::size_t              made = 0;
    for (const BuildUnit& unit : plan.units) {
        if (unit.stale && !fs::exists(unit.objectPath, ec)) {
            failed.push_back(unit.virtualSource);
            continue;
        }
        if (unit.needsLink && !fs::exists(unit.libraryPath, ec)) {
            failed.push_back(unit.virtualSource);
            continue;
        }
        made += unit.stale ? 1 : 0;
    }

    // Counted from the same evidence as the failures - the files on disk -
    // rather than from the plan, so a unit that compiled and then failed to
    // link is not counted as built. See the summary in FinishBuild.
    result.compiled = made;

    // ======================================================================
    //  WHAT THE THINGS IN .build/ WERE BUILT AS - RECORDED EVEN IF THE BUILD
    //  FAILED.
    //
    //  This used to be written only after a SUCCESSFUL compile, with the
    //  reasoning that a failed build should not leave a stamp claiming the
    //  library matches. That conflated two different facts, and the second one
    //  is what the stamp is actually for: not "does the library work" but
    //  "which configuration are the objects and the precompiled header in this
    //  folder built for".
    //
    //  THE COMPILER HAS ALREADY RUN BY THIS LINE. The precompiled header and
    //  every object that did compile are now in THIS configuration whether or
    //  not one file failed, so a stamp still naming the previous one is simply
    //  untrue - and it is believed. Observed consequence: switch to Debug with
    //  one script broken, then build in Release again; the stamp still said
    //  Release, so nothing was forced, the Debug precompiled header was reused
    //  for a /MD compile, and the link failed with four unresolved __imp__*_dbg
    //  symbols that have nothing to do with the broken script.
    //
    //  NOTHING STALE SURVIVES THIS. When the configuration changed, PlanBuild
    //  deleted every object AND every library, so the only libraries left are
    //  ones built just now; a file that failed has no library at all and its
    //  scripts honestly show as NOT FOUND. The failed files are retried next
    //  time because their OBJECTS are missing, which is what has always made
    //  them retry - never the stamp.
    // ======================================================================
    {
        std::ofstream stamp(BuildStampFile(), std::ios::trunc);
        stamp << CurrentBuildStamp();
    }

    if (!failed.empty()) {
        result.ok = false;
        result.summary =
            std::to_string(failed.size()) + " of " + std::to_string(plan.units.size()) +
            " file(s) did not compile - the rest were built";

        // ---- a likely explanation, printed BEFORE the evidence ------------
        //
        // First, so it is read rather than scrolled past after fifty lines of
        // mangled symbol names. The compiler's own output still follows in
        // full - this adds a hint, it does not replace anything.
        //
        // DELIBERATELY WORDED AS A POSSIBILITY. The check is two file
        // timestamps, and a header can be newer than the library without the
        // library being wrong: switching branches rewrites timestamps, and
        // ninja will not relink when a rebuilt object turns out identical. So
        // this can appear beside a genuine mistake in a script, and must not
        // tell anybody their code is fine when it is not.
        const bool unresolved =
            g_lastOutput.find("unresolved external symbol") != std::string::npos;
        const bool mentionsEngine = g_lastOutput.find("eng::") != std::string::npos;

        std::string staleDetail;
        if (unresolved && mentionsEngine && EngineLibraryIsStale(staleDetail)) {
            ENGINE_LOG_ERROR(eng::Channels::kEditor,
                             "NOTE: the link failed on eng:: symbols, and the engine "
                             "library is OLDER than the engine headers. If those "
                             "symbols are functions you did not write, the library "
                             "simply does not have them yet:");
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "  '{}'", staleDetail);
            ENGINE_LOG_ERROR(eng::Channels::kEditor,
                             "  Rebuild the engine in THIS configuration ({}) and try "
                             "again - Debug and Release have their own engine library, "
                             "and building one leaves the other behind.",
                             ENGINE_SCRIPT_IS_DEBUG ? "Debug" : "Release");
        }

        // ---- WHICH ONES, named, before the compiler's output --------------
        //
        // The thing somebody needs first is the list of files that are now not
        // what they think they are. The compiler's own output follows in full.
        for (const std::string& source : failed) {
            const auto broken = std::find_if(
                plan.units.begin(), plan.units.end(),
                [&source](const BuildUnit& u) { return u.virtualSource == source; });

            const bool keptLast = broken != plan.units.end() &&
                                  !broken->libraryPath.empty() &&
                                  fs::exists(broken->libraryPath, ec);

            if (keptLast) {
                // Said plainly, because the alternative is somebody fixing a
                // script, seeing the old behaviour, and concluding the editor
                // is broken. The LAST GOOD library is still there and still
                // loaded - which is what keeps the game playable while one
                // file is mid-edit, but only helps if it is not a surprise.
                ENGINE_LOG_ERROR(eng::Channels::kEditor,
                                 "  {} did not compile - the version from the last "
                                 "successful build is still what runs",
                                 source);
            } else if (broken != plan.units.end() && broken->discardedLibrary) {
                // A THIRD CASE, and it needs its own sentence because the one
                // below would be a lie. There WAS a working library; it was
                // built for a different engine, and PlanBuild threw it away
                // rather than let a library built against other headers be
                // loaded into this process. Saying "never been built" here
                // would send somebody looking for a build that did happen.
                ENGINE_LOG_ERROR(eng::Channels::kEditor,
                                 "  {} did not compile, and the build it had was made "
                                 "for a different engine - it was discarded rather than "
                                 "loaded, so its scripts will show as NOT FOUND until "
                                 "this compiles",
                                 source);
            } else {
                ENGINE_LOG_ERROR(eng::Channels::kEditor,
                                 "  {} did not compile, and has never been built - its "
                                 "scripts will show as NOT FOUND",
                                 source);
            }
        }

        ENGINE_LOG_ERROR(eng::Channels::kEditor, "The compiler said:");
        // Reported LINE BY LINE rather than as one blob, so the Console's
        // filtering and scrolling work on it and the first error is findable.
        std::istringstream lines(g_lastOutput);
        std::string        line;
        while (std::getline(lines, line)) {
            if (!line.empty()) {
                ENGINE_LOG_ERROR(eng::Channels::kEditor, "  {}", line);
            }
        }

        // RETURNS, BUT NOT EMPTY-HANDED. Everything that did build is loaded
        // by FinishBuild, which is the point of building them separately - one
        // broken file used to mean no scripts at all ran.
        //
        // The SOURCE LIST below is deliberately not written, so the next build
        // re-notices whatever changed about the set of files. The STAMP is
        // written above this, on purpose and regardless of failure; the note
        // there explains what went wrong when it was not.
        return result;
    }

    if (exitCode != 0) {
        // The compiler was unhappy and yet every target exists. A warning
        // somebody should see, not a failure - reported without claiming the
        // build broke, because it did not.
        ENGINE_LOG_WARN(eng::Channels::kEditor,
                        "the compiler reported something, though everything was built:");
        std::istringstream lines(g_lastOutput);
        std::string        line;
        while (std::getline(lines, line)) {
            if (!line.empty()) {
                ENGINE_LOG_WARN(eng::Channels::kEditor, "  {}", line);
            }
        }
    }

    // Record what went into this build, so a deleted script is noticed later.
    {
        std::ofstream list(SourceListFile(), std::ios::trunc);
        for (const std::string& source : allSources) {
            list << source << "\n";
        }
    }

    // The stamp is written earlier, before the failure path returns - see the
    // long note above it for why it has to happen even when a build fails.

    return result;
}

ScriptBuild::Result ScriptBuild::FinishBuild(Result compiled) {
    Result        result  = std::move(compiled);
    const Sources sources = GatherSources();

    // ======================================================================
    //  LOADED EVEN WHEN THE BUILD PARTLY FAILED, and that is the change that
    //  makes separate libraries worth having.
    //
    //  It used to return here: one library meant a failed compile produced
    //  nothing, so there was nothing to load and every script in the project
    //  showed as NOT FOUND over a typo in one file. Now everything that built
    //  is loaded, the broken one keeps whatever it had, and Compile has
    //  already said in the Console which file that is and what it means.
    // ======================================================================
    std::string loadError;
    const bool  loaded = eng::ScriptLibrary::LoadModules(
        eng::ScriptLibrary::ModuleDirVirtualPath(), loadError);

    if (!loaded && result.ok) {
        // Compiled, and then would not load. A different failure from a
        // compile error and worth saying differently: the code is valid C++
        // and something about the library itself is wrong.
        result.ok      = false;
        result.summary = "the scripts compiled but the result would not load";
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "{}: {}", result.summary, loadError);
        RememberBuildOutcome(result, sources);
        return result;
    }

    if (!result.ok) {
        // The summary from Compile already names what failed and how many.
        // Everything else is loaded and running.
        RememberBuildOutcome(result, sources);
        return result;
    }

    // ---- did any class change its name? -----------------------------------
    VerifyRegistrations(sources);

    // SAYS HOW MUCH WAS ACTUALLY COMPILED. It used to read "built 11 file(s)"
    // whatever happened, which is a lie most of the time - a build that finds
    // everything current compiles nothing at all, and claiming otherwise is
    // exactly the sort of thing that makes somebody stop believing the
    // incremental part works.
    //
    // THE COUNT IS THE FILES COMPILED, NOT THE SCRIPTS THAT EXIST. Those were
    // the same number only back when every build rebuilt everything, and
    // reporting the total after recompiling one file says precisely the thing
    // this build was changed to stop doing.
    //
    // Nothing compiled but rebuilt anyway means the objects were current and
    // only a library had to be remade - worth saying, because "built 0
    // file(s)" reads like a failure and this is a success.
    const std::string did =
        !result.rebuilt      ? "scripts already up to date"
        : result.compiled == 0
            ? "relinked"
            : "built " + std::to_string(result.compiled) + " file(s)";
    result.summary = did + ", " + std::to_string(eng::ScriptLibrary::ScriptCount()) +
                     " script(s) available";
    ENGINE_LOG_INFO(eng::Channels::kEditor, "{}", result.summary);
    RememberBuildOutcome(result, sources);
    return result;
}

ScriptBuild::Result ScriptBuild::BuildBundle() {
    Result          result;
    EnsureCompilerEnvironment();
    const Sources   sources = GatherSources();
    std::error_code ec;

    const fs::path output = OutputLibrary();

    if (sources.Empty()) {
        // A project with no scripts is a legitimate thing to export. Removing
        // a bundle left over from scripts that have since been deleted is the
        // point of doing anything here at all.
        fs::remove(output, ec);
        result.ok      = true;
        result.summary = "no scripts to bundle";
        return result;
    }

    if (!g_haveCompiler) {
        result.ok      = false;
        result.summary = "there is no C++ compiler to build the scripts with";
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "{}", result.summary);
        return result;
    }

    const bool      stampChanged = CurrentBuildStamp() != ReadWholeFile(BuildStampFile());
    const BuildPlan plan         = PlanBuild(sources, stampChanged);

    const fs::path              buildDir  = BuildDirectory();
    const fs::path              scriptLog = buildDir / "bundle.log";
    const fs::path              engineLib = EngineLinkLibrary();
    const std::vector<fs::path> includes  = IncludeDirectories();

    std::ostringstream cmd;

#if defined(_WIN32)
    const fs::path batPath = buildDir / "bundle.bat";
    {
        std::ofstream bat(batPath, std::ios::trunc);
        bat << "@echo off\n";
        bat << "REM Written by the editor when a game is exported. Safe to read, and\n";
        bat << "REM safe to run by hand.\n";
        if (!g_vcvarsPath.empty()) {
            bat << "call \"" << g_vcvarsPath << "\" >nul 2>&1\n";
        }

        if (plan.usePch && plan.pchStale) {
            bat << "cl.exe /nologo /c /std:c++20 /EHsc";
            bat << (ENGINE_SCRIPT_IS_DEBUG ? " /MDd" : " /MD");
            bat << " /W3";
            for (const fs::path& include : includes) {
                bat << " /I\"" << include.string() << "\"";
            }
            bat << " /Yc\"" << PchHeaderName() << "\"";
            bat << " /Fp\"" << PchFilePath().string() << "\"";
            bat << " /Fo\"" << PchObjectPath().string() << "\"";
            bat << " \"" << PchStubPath().string() << "\"\n";
        }

        if (plan.anyToCompile) {
            bat << "cl.exe /nologo /c /MP /std:c++20 /EHsc";
            bat << (ENGINE_SCRIPT_IS_DEBUG ? " /MDd" : " /MD");
            bat << " /W3";
            for (const fs::path& include : includes) {
                bat << " /I\"" << include.string() << "\"";
            }
            if (plan.usePch) {
                bat << " /Yu\"" << PchHeaderName() << "\"";
                bat << " /Fp\"" << PchFilePath().string() << "\"";
            }
            bat << " /Fo\"" << ObjectDirectory().string() << "\\\\\"";
            for (const BuildUnit& unit : plan.units) {
                if (unit.stale) {
                    bat << " \"" << unit.unitPath.string() << "\"";
                }
            }
            bat << "\n";
        }

        // ONE LINK, EVERY OBJECT. No `if exist` guard here, deliberately: an
        // export that is missing a script is not an export anybody wants, so
        // this one is all-or-nothing and the linker's complaint about the
        // missing object is exactly the right error.
        bat << "cl.exe /nologo /LD";
        bat << (ENGINE_SCRIPT_IS_DEBUG ? " /MDd" : " /MD");
        for (const BuildUnit& unit : plan.units) {
            bat << " \"" << unit.objectPath.string() << "\"";
        }
        if (plan.usePch) {
            bat << " \"" << PchObjectPath().string() << "\"";
        }
        bat << " /Fe\"" << output.string() << "\"";
        bat << " /link \"" << engineLib.string() << "\"";
        bat << " /IMPLIB:\"" << (ObjectDirectory() / "userContent.lib").string() << "\"";
        bat << "\n";
    }
    cmd << "\"" << batPath.string() << "\"";
#else
    const fs::path shPath = buildDir / "bundle.sh";
    {
        std::ofstream sh(shPath, std::ios::trunc);
        sh << "#!/bin/sh\nset -e\n";
        for (const BuildUnit& unit : plan.units) {
            if (!unit.stale) {
                continue;
            }
            sh << g_compilerPath << " -std=c++20 -fPIC -Wall -c";
            for (const fs::path& include : includes) {
                sh << " -I\"" << include.string() << "\"";
            }
            sh << " -o \"" << unit.objectPath.string() << "\"";
            sh << " \"" << unit.unitPath.string() << "\"\n";
        }
        sh << g_compilerPath << " -shared -o \"" << output.string() << "\"";
        for (const BuildUnit& unit : plan.units) {
            sh << " \"" << unit.objectPath.string() << "\"";
        }
        sh << " \"" << engineLib.string() << "\"\n";
    }
    cmd << "sh \"" << shPath.string() << "\"";
#endif

    // The bundle is always relinked rather than tested for staleness. It is
    // one link of objects that are already current, it happens once per
    // export, and "the library in the game I just shipped is the one I just
    // built" is worth more than a second saved.
    fs::remove(output, ec);

    const int exitCode = RunCaptured(cmd.str(), scriptLog);
    g_lastOutput       = ReadWholeFile(scriptLog);
    result.output      = g_lastOutput;
    result.rebuilt     = true;

    // ======================================================================
    //  AND THE STAMP, which this function read at the top and never wrote.
    //
    //  That asymmetry was a trap. An export reads the stamp to decide whether
    //  to force a rebuild, and with a changed stamp PlanBuild deletes and
    //  recompiles EVERY object - and the per-script libraries with them - for
    //  the exporting editor's configuration. Leaving the old stamp behind
    //  meant .build/ then held objects from one configuration and a stamp
    //  naming the other.
    //
    //  WHAT THAT DID, and it is not theoretical: a project last built by the
    //  Debug editor, exported by the Release editor (which is what a build
    //  server or a launcher does), then reopened in the Debug editor. The
    //  stamp still said Debug, so nothing was forced; the objects were /MD;
    //  the libraries had been deleted, so every script needed relinking - and
    //  `cl /LD /MDd` over /MD objects is LNK2038 on every single script, with
    //  nothing pointing at the export that caused it and no way out but
    //  deleting .build/ by hand. It does not even heal from editing a script,
    //  because the precompiled header is in the wrong configuration too.
    //
    //  Written here, after the compiler has run, for the same reason as in
    //  Compile: the stamp describes what the objects in this folder ARE.
    //  Headless exports never bring the scripts subsystem up, so the player's
    //  gate cannot catch this - the stamp is the only record there is.
    // ======================================================================
    {
        std::ofstream stamp(BuildStampFile(), std::ios::trunc);
        stamp << CurrentBuildStamp();
    }

    if (!fs::exists(output, ec)) {
        result.ok      = false;
        result.summary = "the scripts would not bundle into one library";
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "{}. The compiler said:",
                         result.summary);
        std::istringstream lines(g_lastOutput);
        std::string        line;
        while (std::getline(lines, line)) {
            if (!line.empty()) {
                ENGINE_LOG_ERROR(eng::Channels::kEditor, "  {}", line);
            }
        }
        return result;
    }

    if (exitCode != 0) {
        ENGINE_LOG_WARN(eng::Channels::kEditor,
                        "the bundle was produced, and the compiler still had something "
                        "to say about it - see .build/bundle.log");
    }

    result.ok = true;
    result.summary =
        "bundled " + std::to_string(plan.units.size()) + " file(s) into one library";
    ENGINE_LOG_INFO(eng::Channels::kEditor, "{}", result.summary);
    return result;
}

ScriptBuild::Result ScriptBuild::BuildAndReload() {
    Result result;
    if (!BeginBuild(result)) {
        return result;   // nothing to do, or no compiler
    }
    return FinishBuild(Compile());
}

// ===========================================================================
//  THE ASYNCHRONOUS PATH
// ===========================================================================
namespace {

// ONE POOL FOR THE WHOLE EDITOR, created the first time a build is asked for
// and shut down when the program ends.
//
// A pool created per build would start a worker per core EVERY TIME, which
// is the opposite of what a pool is for. A function-local static is created
// once, on first use, with no start-up cost for an editor session that never
// builds anything.
threading::ThreadPool& BuildPool() {
    static threading::ThreadPool pool{1};   // one worker: builds are serialised anyway
    return pool;
}

// The build in flight, if any. A future rather than a raw thread so there is
// something to wait on, and so an exception inside the compile surfaces here
// rather than terminating the program.
std::future<ScriptBuild::Result> g_pending;
std::atomic<bool>                g_building{false};

} // namespace

bool ScriptBuild::IsBuilding() {
    return g_building.load(std::memory_order_acquire);
}

bool ScriptBuild::BeginAsync() {
    // THE GUARD. Everything else in this file is ordinary; this one line is
    // what stops one edit becoming a pile of compiler processes. See the note
    // on BeginAsync in the header for why the blocking version never needed
    // it.
    if (IsBuilding()) {
        return false;
    }

    Result immediate;
    if (!BeginBuild(immediate)) {
        // Nothing to compile, or no compiler. There is no worker to wait for,
        // so the answer is ready now - parked where TakeResult will find it
        // so callers have one way of collecting a result rather than two.
        g_building.store(true, std::memory_order_release);

        std::promise<Result> done;
        done.set_value(std::move(immediate));
        g_pending = done.get_future();
        return false;
    }

    g_building.store(true, std::memory_order_release);
    g_pending = BuildPool().submit([] { return Compile(); });
    return true;
}

bool ScriptBuild::TakeResult(Result& out) {
    if (!g_pending.valid()) {
        return false;
    }

    // POLLED, NOT WAITED ON. This is called once a frame from the main loop,
    // and blocking here for even a moment is the lock-up this whole change
    // exists to remove.
    if (g_pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return false;
    }

    Result compiled = g_pending.get();   // also clears the future
    g_building.store(false, std::memory_order_release);

    // THE LOAD HAPPENS HERE, on whichever thread called TakeResult - which is
    // the main one. It recreates and rebinds every script object in the live
    // scene, so it cannot run on the worker. See BeginBuild.
    out = FinishBuild(std::move(compiled));
    return true;
}

void ScriptBuild::WaitForBuild() {
    if (!g_pending.valid()) {
        return;
    }
    Result ignored;
    g_pending.wait();
    TakeResult(ignored);
}

// ---------------------------------------------------------------------------
//  Checking the registrations against last time
//
//  Renaming a class is an ordinary thing to do in a text editor, and it is
//  invisible to everything else: the file keeps its name, it still compiles,
//  and it still registers a script. But the SCENE refers to the class by its
//  old name, so every entity using it silently becomes unresolved and stops
//  doing anything.
//
//  So after every successful build the editor compares what each file
//  registers now against what it registered last time. When one class in a
//  file has been replaced by exactly one other, that is a rename, and the
//  components using it are moved across and the move is reported. Anything
//  less clear-cut is reported and left alone - guessing would be worse.
//
//  The scene is NOT saved by this. Nothing is written to disk, so an
//  unwanted rebind is undone by not saving.
// ---------------------------------------------------------------------------
void ScriptBuild::VerifyRegistrations(const Sources& sources) {
    using ClassesByFile = std::map<std::string, std::set<std::string>>;

    // ---- what is registered right now, straight from the engine ------------
    ClassesByFile current;
    eng::ScriptRegistry::ForEachEntry(
        [&current](const char* name, const eng::ScriptRegistry::Entry& entry) {
            current[ToVirtualPath(fs::path(entry.sourceFile))].insert(name);
        });

    // ---- what was registered after the previous build -----------------------
    ClassesByFile previous;
    {
        std::istringstream lines(ReadWholeFile(ClassManifestFile()));
        std::string        line;
        while (std::getline(lines, line)) {
            const std::size_t tab = line.find('\t');
            if (tab == std::string::npos) {
                continue;
            }
            const std::string file = line.substr(0, tab);
            std::istringstream names(line.substr(tab + 1));
            std::string        name;
            while (std::getline(names, name, ',')) {
                if (!name.empty()) {
                    previous[file].insert(name);
                }
            }
        }
    }

    // ---- compare, file by file ---------------------------------------------
    for (const auto& [file, before] : previous) {
        const auto        it    = current.find(file);
        const std::set<std::string> after = (it != current.end()) ? it->second
                                                                  : std::set<std::string>{};

        std::vector<std::string> gone;
        std::vector<std::string> added;
        std::set_difference(before.begin(), before.end(), after.begin(), after.end(),
                            std::back_inserter(gone));
        std::set_difference(after.begin(), after.end(), before.begin(), before.end(),
                            std::back_inserter(added));

        if (gone.empty()) {
            continue;
        }

        // The clear-cut case: one class replaced by exactly one other, in the
        // same file. That is a rename, and it can be followed.
        if (gone.size() == 1 && added.size() == 1) {
            const std::size_t moved =
                eng::ScriptSystem::RebindRenamed(gone.front(), added.front());
            if (moved > 0) {
                ENGINE_LOG_WARN(eng::Channels::kEditor,
                                "'{}' renamed its script class from '{}' to '{}' - "
                                "{} attached component(s) moved across. Save the scene "
                                "to keep the change.",
                                file, gone.front(), added.front(), moved);
            } else {
                ENGINE_LOG_INFO(eng::Channels::kEditor,
                                "'{}' renamed its script class from '{}' to '{}' "
                                "(nothing in the scene was using it)",
                                file, gone.front(), added.front());
            }
            continue;
        }

        // Anything else is reported rather than guessed at.
        for (const std::string& name : gone) {
            const std::size_t using_ = eng::ScriptSystem::CountUsing(name);
            if (using_ > 0) {
                ENGINE_LOG_ERROR(eng::Channels::kEditor,
                                 "the script '{}' no longer exists in '{}', and {} "
                                 "entity component(s) still refer to it - they will not "
                                 "run until the class is back or they are pointed "
                                 "somewhere else",
                                 name, file, using_);
            } else {
                ENGINE_LOG_INFO(eng::Channels::kEditor,
                                "the script '{}' is no longer defined in '{}'", name,
                                file);
            }
        }
    }

    // ---- files that look like scripts but register nothing ------------------
    //
    // Writing the class and forgetting the one line that registers it is the
    // single easiest mistake to make here, and its symptom - the script simply
    // not appearing anywhere - gives no hint about the cause.
    for (const std::string& source : sources.All()) {
        if (current.find(source) != current.end()) {
            continue;   // this file registered something
        }
        const fs::path real = fs::path(eng::FileSystem::Resolve(source));
        if (FileContains(real, "ScriptBehaviour") &&
            !FileContains(real, "ENGINE_REGISTER_SCRIPT")) {
            ENGINE_LOG_WARN(eng::Channels::kEditor,
                            "'{}' defines a ScriptBehaviour but never registers it - add "
                            "ENGINE_REGISTER_SCRIPT(YourClassName) at the bottom of the "
                            "file, or the engine cannot find it",
                            source);
        }
    }

    // ---- record this build's answer for next time ---------------------------
    {
        std::ofstream manifest(ClassManifestFile(), std::ios::trunc);
        for (const auto& [file, names] : current) {
            manifest << file << '\t';
            bool first = true;
            for (const std::string& name : names) {
                if (!first) {
                    manifest << ',';
                }
                manifest << name;
                first = false;
            }
            manifest << '\n';
        }
    }
}

} // namespace editor
