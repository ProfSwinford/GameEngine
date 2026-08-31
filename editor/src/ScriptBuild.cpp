// ============================================================================
//  ScriptBuild.cpp - finding a compiler and using it. See ScriptBuild.h.
//
//  ==========================================================================
//  HOW THE COMPILER IS ACTUALLY RUN
//
//  The editor writes a small build script into scripts/.build/ and runs it,
//  with everything the compiler prints redirected into a log file it then
//  reads back.
//
//  That is deliberately low-tech, and it has one property worth the trouble:
//  the build script is a real file that stays on disk afterwards. When a build
//  does something surprising, you can open scripts/.build/build.bat and read
//  the exact command that ran, or run it yourself in a terminal. A build step
//  hidden inside the program would give you nothing to look at.
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
#include <engine/scene/ScriptLibrary.h>

// SDL is used for one thing: asking where the editor's own executable is, so a
// released copy can find the headers it ships beside itself. The editor
// already links SDL for ImGui's backend, so this costs nothing extra.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
    return fs::path(eng::FileSystem::Resolve("scripts/.build"));
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
    if (IsOnPath("cl.exe")) {
        g_compilerPath        = "cl.exe";
        g_haveCompiler        = true;
        g_compilerDescription = "MSVC (cl.exe, already on PATH)";
    } else if (const std::string vcvars = FindVisualStudioVcvars(); !vcvars.empty()) {
        // Usual case: Visual Studio is installed but its compiler is not on
        // the PATH, so the build script runs vcvars64.bat first to set it up.
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

std::vector<std::string> ScriptBuild::GatherSources() {
    std::vector<std::string> sources;

    std::vector<eng::FileSystem::DirEntry> entries;
    if (!eng::FileSystem::ListDirectory("scripts", entries)) {
        return sources;   // no scripts folder yet, which is a normal state
    }

    for (const eng::FileSystem::DirEntry& entry : entries) {
        if (!entry.isDirectory && entry.name.ends_with(".cpp")) {
            sources.push_back(entry.virtualPath);
        }
    }
    // Sorted so that the recorded list is comparable between runs rather than
    // depending on the order the operating system happened to hand them back.
    std::sort(sources.begin(), sources.end());
    return sources;
}

bool ScriptBuild::NeedsRebuild() {
    const std::vector<std::string> sources = GatherSources();

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

    const std::vector<std::string> sources = GatherSources();

    std::error_code ec;
    const fs::path  buildDir = BuildDirectory();
    fs::create_directories(buildDir, ec);

    // ---- nothing to compile ----------------------------------------------
    if (sources.empty()) {
        eng::ScriptLibrary::Unload();
        fs::remove(OutputLibrary(), ec);
        std::ofstream(SourceListFile(), std::ios::trunc);   // record: nothing built

        result.rebuilt = true;
        result.summary = "no scripts in scripts/ - nothing to build";
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
        for (const std::string& source : sources) {
            bat << " \"" << eng::FileSystem::Resolve(source) << "\"";
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
        for (const std::string& source : sources) {
            sh << " \"" << eng::FileSystem::Resolve(source) << "\"";
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
        for (const std::string& source : sources) {
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

    result.summary = "built " + std::to_string(sources.size()) + " script(s), " +
                     std::to_string(eng::ScriptLibrary::ScriptCount()) + " available";
    ENGINE_LOG_INFO(eng::Channels::kEditor, "{}", result.summary);
    return result;
}

} // namespace editor
