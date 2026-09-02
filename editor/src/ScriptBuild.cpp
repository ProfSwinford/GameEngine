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
#include <engine/scene/ScriptComponent.h>
#include <engine/scene/ScriptLibrary.h>

// SDL is used for one thing: asking where the editor's own executable is, so a
// released copy can find the headers it ships beside itself. The editor
// already links SDL for ImGui's backend, so this costs nothing extra.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

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
#if defined(_WIN32)
    // THE EXTRA PAIR OF QUOTES AROUND THE WHOLE THING IS NOT A TYPO.
    //
    // std::system runs the command through cmd.exe, and cmd strips one leading
    // and one trailing quote from the line before doing anything else. Both the
    // program's path and the log file's path can contain spaces - "C:\Program
    // Files (x86)\..." certainly does - so both need quoting, and without the
    // outer pair cmd mis-reads where the command ends and the redirect begins.
    //
    // Getting this wrong does not produce an error. The command silently does
    // nothing and reports failure, which looks exactly like "no compiler
    // installed".
    const std::string full = "\"" + command + " > \"" + logPath.string() + "\" 2>&1\"";
#else
    const std::string full = command + " > \"" + logPath.string() + "\" 2>&1";
#endif
    return std::system(full.c_str());
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

#endif // _WIN32

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

bool ScriptBuild::NeedsRebuild() {
    const std::vector<std::string> sources = GatherSources().All();

    std::error_code ec;
    const fs::path  library = OutputLibrary();
    const bool      haveLibrary = fs::exists(library, ec);

    if (sources.empty()) {
        // Nothing to build. If a library is left over from scripts that have
        // all since been deleted, it does need rebuilding away - but there is
        // nothing to compile, so that is handled by the source-list check
        // below rather than here.
        return haveLibrary && !ReadWholeFile(SourceListFile()).empty();
    }

    if (!haveLibrary) {
        return true;   // scripts exist and nothing has been built yet
    }

    // Did the SET of scripts change? This is what notices a deleted or renamed
    // file, which a timestamp comparison cannot.
    std::string current;
    for (const std::string& source : sources) {
        current += source + "\n";
    }
    if (current != ReadWholeFile(SourceListFile())) {
        return true;
    }

    // Is any script newer than the library built from it?
    const auto libraryTime = fs::last_write_time(library, ec);
    if (ec) {
        return true;
    }
    for (const std::string& source : sources) {
        const auto sourceTime =
            fs::last_write_time(fs::path(eng::FileSystem::Resolve(source)), ec);
        if (!ec && sourceTime > libraryTime) {
            return true;
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
//  Building
// ---------------------------------------------------------------------------

ScriptBuild::Result ScriptBuild::BuildAndReload() {
    Result result;

    const Sources                  sources    = GatherSources();
    const std::vector<std::string> allSources = sources.All();

    std::error_code ec;
    const fs::path  buildDir = BuildDirectory();
    fs::create_directories(buildDir, ec);

    // ---- nothing to compile ----------------------------------------------
    if (sources.Empty()) {
        eng::ScriptLibrary::Unload();
        fs::remove(OutputLibrary(), ec);
        std::ofstream(SourceListFile(), std::ios::trunc);      // record: nothing built
        std::ofstream(ClassManifestFile(), std::ios::trunc);

        result.rebuilt = true;
        result.summary = "no scripts found in assets/ - nothing to build";
        ENGINE_LOG_INFO(eng::Channels::kEditor, "{}", result.summary);
        return result;
    }

    if (!g_haveCompiler) {
        result.ok      = false;
        result.summary = "scripts changed, but there is no C++ compiler to build them";
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "{} ({})", result.summary,
                         "see the message at start-up for what to install");
        return result;
    }

    // ---- unload BEFORE building ------------------------------------------
    //
    // Two reasons, and both are hard failures rather than untidiness. Windows
    // refuses to overwrite a file that is currently loaded, so the link step
    // would simply fail. And every script object in the running scene was
    // created by code inside this library; destroying them first is the only
    // way they can be destroyed at all.
    eng::ScriptLibrary::Unload();

    // ---- write the build script ------------------------------------------
    const fs::path       output    = OutputLibrary();
    const fs::path       scriptLog = buildDir / "build.log";
    const fs::path       engineLib = EngineLinkLibrary();
    std::vector<fs::path> includes = IncludeDirectories();

    // The list actually handed to the compiler: every .cpp, plus one generated
    // file that includes the scripts written in headers.
    std::vector<std::string> compileList;
    for (const std::string& source : sources.compiled) {
        compileList.push_back(eng::FileSystem::Resolve(source));
    }

    if (!sources.headers.empty()) {
        // Generated, and left on disk on purpose - if a header script will not
        // compile, this is the file the error refers to, and being able to
        // open it is the difference between a confusing message and an
        // obvious one.
        std::ofstream unit(GeneratedHeaderUnit(), std::ios::trunc);
        unit << "// Written by the editor. Do not edit - it is regenerated on every\n";
        unit << "// build from the .h files under assets/ that register a script.\n";
        unit << "//\n";
        unit << "// A header is not compiled on its own. Without this file, a script\n";
        unit << "// written entirely in a .h would be listed in the Assets panel and\n";
        unit << "// would never run.\n";
        for (const std::string& header : sources.headers) {
            // Forward slashes even on Windows: a backslash inside an #include
            // is an escape sequence as far as the preprocessor is concerned.
            unit << "#include \""
                 << fs::path(eng::FileSystem::Resolve(header)).generic_string()
                 << "\"\n";
        }
        compileList.push_back(GeneratedHeaderUnit().string());
    } else {
        // No header scripts any more. Delete the generated unit rather than
        // leaving a stale one behind - .build/ is a folder people are told
        // they can open and read, so a file in it that is no longer part of
        // the build is just something to be misled by.
        fs::remove(GeneratedHeaderUnit(), ec);
    }

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
            // Both its output streams are thrown away. This script is chatty,
            // and on some machines it complains about its own internals in
            // ways that have nothing to do with your code - and every one of
            // those lines would otherwise be reported to you as a compiler
            // error. What ends up in the log should be what the COMPILER said.
            bat << "call \"" << g_vcvarsPath << "\" >nul 2>&1\n";
        }

        bat << "cl.exe /nologo /std:c++20 /EHsc /LD";
        // The C runtime setting has to match the engine's - see ScriptBuild.h.
        bat << (ENGINE_SCRIPT_IS_DEBUG ? " /MDd" : " /MD");
        // Warnings are kept modest on purpose: this is somebody's game code,
        // not the engine, and an unused parameter in a half-written script
        // should not produce a wall of output.
        bat << " /W3";

        for (const fs::path& include : includes) {
            bat << " /I\"" << include.string() << "\"";
        }
        for (const std::string& source : compileList) {
            bat << " \"" << source << "\"";
        }

        bat << " /Fo\"" << buildDir.string() << "\\\\\"";
        bat << " /Fe\"" << output.string() << "\"";
        bat << " /link \"" << engineLib.string() << "\"\n";
    }
    cmd << "\"" << batPath.string() << "\"";
#else
    const fs::path shPath = buildDir / "build.sh";
    {
        std::ofstream sh(shPath, std::ios::trunc);
        sh << "#!/bin/sh\n";
        sh << "# Written by the editor. Safe to read, and safe to run by hand.\n";
        sh << g_compilerPath << " -std=c++20 -shared -fPIC -Wall";
        for (const fs::path& include : includes) {
            sh << " -I\"" << include.string() << "\"";
        }
        for (const std::string& source : compileList) {
            sh << " \"" << source << "\"";
        }
        sh << " -o \"" << output.string() << "\"";
        sh << " \"" << engineLib.string() << "\"\n";
    }
    cmd << "sh \"" << shPath.string() << "\"";
#endif

    // ---- run it -----------------------------------------------------------
    const int exitCode = RunCaptured(cmd.str(), scriptLog);
    g_lastOutput       = ReadWholeFile(scriptLog);
    result.output      = g_lastOutput;
    result.rebuilt     = true;

    if (exitCode != 0) {
        result.ok      = false;
        result.summary = "the scripts did not compile";

        ENGINE_LOG_ERROR(eng::Channels::kEditor,
                         "the scripts did not compile. The compiler said:");
        // Reported LINE BY LINE rather than as one blob, so the Console's
        // filtering and scrolling work on it and the first error is findable.
        std::istringstream lines(g_lastOutput);
        std::string        line;
        while (std::getline(lines, line)) {
            if (!line.empty()) {
                ENGINE_LOG_ERROR(eng::Channels::kEditor, "  {}", line);
            }
        }

        // The old library has already been unloaded, so every script now shows
        // as NOT FOUND in the Inspector. That is honest: the code that was
        // running has been replaced by code that does not compile.
        return result;
    }

    // Record what went into this build, so a deleted script is noticed later.
    {
        std::ofstream list(SourceListFile(), std::ios::trunc);
        for (const std::string& source : allSources) {
            list << source << "\n";
        }
    }

    // ---- load the result --------------------------------------------------
    std::string loadError;
    if (!eng::ScriptLibrary::Load(eng::ScriptLibrary::DefaultVirtualPath(), loadError)) {
        result.ok      = false;
        result.summary = "the scripts compiled but the result would not load";
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "{}: {}", result.summary, loadError);
        return result;
    }

    // ---- did any class change its name? -----------------------------------
    VerifyRegistrations(sources);

    result.summary = "built " + std::to_string(sources.Count()) + " file(s), " +
                     std::to_string(eng::ScriptLibrary::ScriptCount()) + " script(s) available";
    ENGINE_LOG_INFO(eng::Channels::kEditor, "{}", result.summary);
    return result;
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
