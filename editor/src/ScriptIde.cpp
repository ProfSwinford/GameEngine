// ============================================================================
//  ScriptIde.cpp - writing the two files an IDE reads, and opening the IDE.
//
//  What these files are for, why there are two of them, why they are written
//  only when asked and why they are not worth committing: ScriptIde.h.
// ============================================================================

#include "ScriptIde.h"

#include "ScriptBuild.h"

#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace editor {
namespace {

namespace fs = std::filesystem;

constexpr const char* kCppPropertiesFile  = "CppProperties.json";
constexpr const char* kCompileCommandsFile = "compile_commands.json";

// ---------------------------------------------------------------------------
//  WRITTEN AS TEXT RATHER THAN BUILT WITH NLOHMANN, and for the reason
//  Project.cpp gives about config/engine.json: a generated file that explains
//  itself is worth more than one that round-trips perfectly, and there is no
//  way to make a JSON library emit a comment.
//
//  CppProperties.json can carry comments - Microsoft's own documented example
//  has them - and it is the file somebody will open when IntelliSense is not
//  working, so it had better say what it is and who wrote it.
//
//  compile_commands.json gets NONE, because clangd parses it as strict JSON
//  and a comment would make the whole file unreadable. That is why the
//  explanation for that one lives in ScriptIde.h and in the status line
//  instead of in the file.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
//  JSON IS UTF-8. A PATH THAT REACHED US AS std::string IS NOT.
//
//  std::filesystem::path::string() converts from the wide native form using
//  the system ANSI code page, so a project under a folder called "josé-game"
//  arrives here as the single byte 0xE9 where the accent is. That byte cannot
//  stand alone in UTF-8 - 0xE9 announces a three-byte sequence and the next
//  byte is a hyphen - so a database written with it raw is a file a strict
//  parser is entitled to reject whole.
//
//  AND THE SYMPTOM WOULD BE THE ONE THING THIS FEATURE EXISTS TO PREVENT:
//  IntelliSense silently not working, for somebody whose only mistake was
//  having an accent in their user name. Measured rather than assumed - the
//  generated database held `6a 6f 73 e9 2d` for "josé-" before this existed.
//
//  Going back out through the wide form is also an identity when the system
//  code page already IS UTF-8, so there is no second case to handle.
// ---------------------------------------------------------------------------
std::string Utf8(std::string_view text) {
#if defined(_WIN32)
    if (text.empty()) {
        return {};
    }

    const int length = static_cast<int>(text.size());

    const int wide = MultiByteToWideChar(CP_ACP, 0, text.data(), length, nullptr, 0);
    if (wide <= 0) {
        return std::string(text);
    }
    std::wstring utf16(static_cast<std::size_t>(wide), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text.data(), length, utf16.data(), wide);

    const int bytes =
        WideCharToMultiByte(CP_UTF8, 0, utf16.data(), wide, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) {
        return std::string(text);
    }
    std::string out(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, utf16.data(), wide, out.data(), bytes, nullptr,
                        nullptr);
    return out;
#else
    // Paths are bytes here and the convention is UTF-8 already.
    return std::string(text);
#endif
}

// A JSON string, quotes included. Escapes what JSON requires rather than what
// a path is likely to contain: a control character in a filename is absurd,
// and emitting one raw would make the file unparseable for the one consumer
// that is strict about it.
std::string Quoted(std::string_view text) {
    const std::string encoded = Utf8(text);

    std::string out;
    out.reserve(encoded.size() + 2);
    out += '"';

    for (const char c : encoded) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[8] = {};
                std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                              static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buffer;
            } else {
                out += c;
            }
            break;
        }
    }

    out += '"';
    return out;
}

// These land in the project ROOT, which eng::FileSystem cannot address: every
// virtual path it resolves goes under assets/, bar a short list of exceptions.
// So this is an ofstream, exactly as eng::Project::Write writes the manifest
// into the same folder.
bool WriteWholeFile(const fs::path& path, const std::string& text,
                    std::string& outError) {
    // CLOSED EXPLICITLY, AND CHECKED. An ofstream flushes what is still
    // buffered in its DESTRUCTOR and throws the result away, so a write that
    // failed on a full disk or a dropped network share would be reported as a
    // success - and for a file this size most of the bytes are still in the
    // buffer at the end of the loop, so the check after operator<< sees
    // nothing wrong. close() is the one call that reports a failed flush.
    bool ok = false;
    {
        std::ofstream out(path, std::ios::trunc | std::ios::binary);
        if (out) {
            out << text;
            out.close();
            ok = static_cast<bool>(out);
        }
    }

    if (!ok) {
        outError = "could not write " + path.generic_string();

        // AND THE REMAINS ARE REMOVED, which matters more than it looks.
        // std::ios::trunc empties the file at open, so a failure part way
        // leaves a TRUNCATED one - and half a JSON document is not a stale
        // config, it is an invalid one. A tool handed an unparseable compile
        // database drops the flags for every file in it and says nothing,
        // which is precisely the silent failure these files exist to remove.
        // No file at all is the honest state, and the one the caller reports.
        std::error_code ec;
        fs::remove(path, ec);
        return false;
    }

    return true;
}

// Forward slashes, because a backslash is an escape sequence in JSON and this
// string is about to become one. MSVC takes either in /I.
std::string Generic(const std::string& path) {
    return fs::path(path).generic_string();
}

#if defined(_WIN32)
// ---------------------------------------------------------------------------
//  ONE ARGUMENT OF A WINDOWS COMMAND LINE, QUOTED.
//
//  A BACKSLASH IMMEDIATELY BEFORE THE CLOSING QUOTE ESCAPES IT. So a project
//  root that ends in a separator - which is an ordinary thing for a root to
//  do: ENGINE_ASSET_ROOT set with a trailing slash, or a project sitting at
//  C:\ - turns "C:\dev\MyGame\" into an argument that swallows the quote and
//  everything after it. The child sees C:\dev\MyGame" , which cannot exist,
//  CreateProcessW still succeeds, and the editor cheerfully reports that
//  Visual Studio is opening.
//
//  The rule is that every backslash in the run that MEETS the closing quote
//  has to be doubled. Runs in the middle of the path are literal and are left
//  alone. A quote inside the string is not handled, and does not need to be:
//  Windows forbids one in a filename.
// ---------------------------------------------------------------------------
std::wstring QuotedArgument(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size() + 4);
    out += L'"';

    std::size_t backslashes = 0;
    for (const wchar_t c : text) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        out.append(backslashes, L'\\');
        backslashes = 0;
        out += c;
    }

    // The run that ends the string is the one that would eat the quote.
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}
#endif

// ---------------------------------------------------------------------------
//  CppProperties.json - what Visual Studio reads in Open Folder mode.
// ---------------------------------------------------------------------------
bool WriteCppProperties(const fs::path& root,
                        const ScriptBuild::CompileSettings& settings,
                        std::string& outError) {
    // The standard and the switches as one string. A STRING, not an array:
    // the schema Visual Studio ships says so, and the array spelling is the
    // habit that comes from VS Code's c_cpp_properties.json, which is a
    // different file with different keys.
    std::ostringstream switches;
    bool               firstSwitch = true;
    for (const std::string& argument : settings.ParseArguments()) {
        switches << (firstSwitch ? "" : " ") << argument;
        firstSwitch = false;
    }

    std::ostringstream text;
    text << "{\n";
    text << "  // Written by the editor, from the flags it compiles this project's\n";
    text << "  // scripts with. Safe to delete: File > Set Up IDE IntelliSense writes\n";
    text << "  // it again, and so does `editor --write-ide-files`.\n";
    text << "  //\n";
    text << "  // NOT WORTH COMMITTING. The include paths below are absolute, and a\n";
    text << "  // released editor and a source tree do not use the same set of them, so\n";
    text << "  // a copy from another machine is wrong rather than merely stale.\n";
    text << "  //\n";
    text << "  // VISUAL STUDIO ONLY READS THIS IN OPEN FOLDER MODE: File > Open >\n";
    text << "  // Folder, on the folder this file sits in. A folder holding a\n";
    text << "  // CMakeLists.txt is treated as a CMake project instead and this file is\n";
    text << "  // ignored there - that case is served by the compile database CMake\n";
    text << "  // generates for itself.\n";
    text << "  \"configurations\": [\n";
    text << "    {\n";
    text << "      \"name\": \"Game scripts\",\n";
    text << "      \"intelliSenseMode\": "
         << Quoted(settings.msvc ? "windows-msvc-x64" : "linux-gcc-x64") << ",\n";
    text << "\n";
    text << "      // The engine's headers, and nlohmann/json's - every component's\n";
    text << "      // Deserialize takes a Json, so a script needs both.\n";
    text << "      \"includePath\": [\n";
    for (std::size_t i = 0; i < settings.includeDirs.size(); ++i) {
        text << "        " << Quoted(settings.includeDirs[i])
             << (i + 1 < settings.includeDirs.size() ? "," : "") << "\n";
    }
    text << "      ],\n";
    text << "\n";
    text << "      // The compiler never sees your .cpp on its own: it sees a generated\n";
    text << "      // file that includes this first and then includes your script. So a\n";
    text << "      // script that names no headers at all still compiles, and this is\n";
    text << "      // what says so.\n";
    text << "      \"forcedInclude\": [\n";
    text << "        " << Quoted(settings.forcedInclude) << "\n";
    text << "      ],\n";
    text << "\n";
    text << "      \"compilerSwitches\": " << Quoted(switches.str()) << "\n";
    text << "    }\n";
    text << "  ]\n";
    text << "}\n";

    return WriteWholeFile(root / kCppPropertiesFile, text.str(), outError);
}

// ---------------------------------------------------------------------------
//  compile_commands.json - what clangd reads, and so everything that is not
//  Visual Studio.
//
//  IT HAS TO LIVE IN THE PROJECT ROOT. clangd walks up from the file being
//  edited and, at each folder, looks for compile_commands.json and then for
//  build/compile_commands.json - the literal name "build", nothing else. A
//  database inside .build/ beside the rest of the generated output would
//  never be found, which is the one place it would otherwise belong.
// ---------------------------------------------------------------------------
void AppendEntry(std::ostream& out, const fs::path& root,
                 const ScriptBuild::CompileSettings& settings,
                 const std::string& virtualPath, bool isHeader, bool last) {
    const std::string file = Generic(eng::FileSystem::Resolve(virtualPath));

    out << "  {\n";
    out << "    \"directory\": " << Quoted(root.generic_string()) << ",\n";
    out << "    \"file\": " << Quoted(file) << ",\n";
    out << "    \"arguments\": [\n";

    std::vector<std::string> arguments;

    // THE DRIVER NAME CARRIES MEANING, which is why it comes from the
    // description and not from whatever compiler happened to be found. clangd
    // decides whether to read the rest of the line as MSVC or as GNU by
    // looking at this one argument: a name not ending in "cl" makes it treat
    // /EHsc and /std:c++20 as unknown options, drop them, and parse every
    // script against the default standard with no _MSC_VER - which surfaces
    // as a thousand errors inside the engine's own headers. See the note on
    // CompileSettings::driver.
    arguments.push_back(settings.driver);

    // Compile, do not link. The output flags are deliberately absent: a
    // compile database describes how to PARSE a file, and /Fo, /Fe and the
    // precompiled header are the parts that differ between the six places
    // the editor writes this command - see AppendParseFlags in
    // ScriptBuild.cpp.
    arguments.push_back(settings.msvc ? "/c" : "-c");

    const std::vector<std::string> parse = settings.ParseArguments();
    arguments.insert(arguments.end(), parse.begin(), parse.end());

    for (const std::string& dir : settings.includeDirs) {
        // JOINED, one argument. Split across two is legal for cl and for
        // clang-cl, but one element per flag is what every consumer agrees
        // about, and the VS Code extension does not unescape these at all.
        arguments.push_back((settings.msvc ? "/I" : "-I") + dir);
    }

    if (settings.msvc) {
        arguments.push_back("/FI" + settings.forcedInclude);
    } else {
        arguments.push_back("-include");
        arguments.push_back(settings.forcedInclude);
    }

    // A HEADER HAS TO BE TYPED EXPLICITLY. Clang reads a .h as C unless told
    // otherwise, so a script written in a header would be parsed as C and
    // report the whole engine as broken. The editor compiles these through a
    // generated .cpp, so cl never needs telling; a parser handed the header
    // directly does.
    if (isHeader) {
        arguments.push_back(settings.msvc ? "/TP" : "-xc++-header");
    }

    arguments.push_back(file);

    for (std::size_t i = 0; i < arguments.size(); ++i) {
        out << "      " << Quoted(arguments[i])
            << (i + 1 < arguments.size() ? "," : "") << "\n";
    }

    out << "    ]\n";
    out << (last ? "  }\n" : "  },\n");
}

bool WriteCompileCommands(const fs::path& root,
                          const ScriptBuild::CompileSettings& settings,
                          const ScriptBuild::Sources& sources,
                          std::size_t& outDescribed, std::string& outError) {
    // The sources that are actually COMPILED. Helper headers are left out on
    // purpose: the editor never hands one to the compiler either, and a
    // header written to be included in one particular place can easily fail
    // to compile on its own. clangd works them out from whichever script
    // includes them, which is the same answer the build gives.
    const std::size_t total = sources.compiled.size() + sources.headers.size();
    outDescribed            = total;

    std::ostringstream text;
    text << "[\n";

    std::size_t written = 0;
    for (const std::string& virtualPath : sources.compiled) {
        ++written;
        AppendEntry(text, root, settings, virtualPath, false, written == total);
    }
    for (const std::string& virtualPath : sources.headers) {
        ++written;
        AppendEntry(text, root, settings, virtualPath, true, written == total);
    }

    text << "]\n";

    return WriteWholeFile(root / kCompileCommandsFile, text.str(), outError);
}

} // namespace

bool WriteIdeFiles(std::string& outSummary, std::string& outError) {
    const std::string projectRoot = eng::FileSystem::AssetRoot();
    if (projectRoot.empty()) {
        outError = "no project is open, so there is nowhere to write these";
        return false;
    }

    // THE FLAGS ARE THE POINT, so no compiler means nothing truthful to say.
    // Writing a plausible-looking config from guessed flags is the one
    // outcome worse than writing none: it would describe a compile that is
    // not the one this editor performs.
    if (!ScriptBuild::HasCompiler()) {
        outError = "no compiler was found, and these files describe the compile - "
                   "install the C++ workload and start the editor again";
        return false;
    }

    const ScriptBuild::CompileSettings settings = ScriptBuild::Settings();
    if (settings.includeDirs.empty()) {
        outError = "the engine's headers could not be located, so there is no "
                   "include path to write";
        return false;
    }

    const fs::path root(projectRoot);

    if (!WriteCppProperties(root, settings, outError)) {
        return false;
    }

    std::size_t described = 0;
    if (!WriteCompileCommands(root, settings, ScriptBuild::GatherSources(), described,
                              outError)) {
        return false;
    }

    std::ostringstream summary;
    summary << "wrote " << kCppPropertiesFile << " and " << kCompileCommandsFile << " ("
            << described << " script" << (described == 1 ? "" : "s") << ")";
    outSummary = summary.str();

    ENGINE_LOG_INFO(eng::Channels::kEditor,
                    "{} into '{}' - Visual Studio reads {} when you open the FOLDER, "
                    "and {} is for clangd and anything built on it",
                    outSummary, root.generic_string(), kCppPropertiesFile,
                    kCompileCommandsFile);
    return true;
}

bool OpenInVisualStudio(std::string& outError) {
#if defined(_WIN32)
    const std::string projectRoot = eng::FileSystem::AssetRoot();
    if (projectRoot.empty()) {
        outError = "no project is open";
        return false;
    }

    const std::string installPath = ScriptBuild::VisualStudioPath();
    if (installPath.empty()) {
        outError = "Visual Studio was not found on this machine";
        return false;
    }

    const fs::path  devenv = fs::path(installPath) / "Common7" / "IDE" / "devenv.exe";
    std::error_code ec;
    if (!fs::exists(devenv, ec)) {
        outError = "Visual Studio is installed but devenv.exe is not where it should "
                   "be: " + devenv.generic_string();
        return false;
    }

    // THE FOLDER, NOT A FILE. devenv takes a solution, a project, a folder or
    // a single file, and which one it is decides what you get: a folder opens
    // Open Folder mode, which is the mode that reads CppProperties.json. Hand
    // it one .cpp and it opens with no project behind it and no include
    // paths - which is exactly the state that made these files necessary.
    const std::wstring command = QuotedArgument(devenv.wstring()) + L" " +
                                 QuotedArgument(fs::path(projectRoot).wstring());

    // CreateProcessW writes into its command line, so it gets a buffer of its
    // own rather than the string's.
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');

    STARTUPINFOW        startup{};
    PROCESS_INFORMATION process{};
    startup.cb = static_cast<DWORD>(sizeof(startup));

    if (CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       nullptr, &startup, &process) == 0) {
        outError = "could not start Visual Studio (error " +
                   std::to_string(GetLastError()) + ")";
        return false;
    }

    // Closed, not waited on. The editor is not its parent in any meaningful
    // sense and must not block: Visual Studio takes its time to start, and
    // leaking both handles for the life of the editor is the alternative.
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    ENGINE_LOG_INFO(eng::Channels::kEditor, "opened '{}' in Visual Studio",
                    fs::path(projectRoot).generic_string());
    return true;
#else
    outError = "Visual Studio only exists on Windows";
    return false;
#endif
}

} // namespace editor
