// ============================================================================
//  Project.cpp - reading, writing and creating a project folder.
//  See Project.h for why none of this goes through FileSystem::Resolve.
// ============================================================================

#include <engine/project/Project.h>

#include <engine/core/Log.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace eng {
namespace {

// ---------------------------------------------------------------------------
//  Reading and writing whole small files, with std::filesystem and nothing
//  else.
//
//  FileSystem has perfectly good ReadTextFile and WriteTextFile and this file
//  may not use them: they take VIRTUAL paths, and virtual paths need the root
//  this code is in the business of finding. See the header.
// ---------------------------------------------------------------------------
bool ReadWholeFile(const fs::path& path, std::string& outText, std::string& outError) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        outError = "cannot open '" + path.string() + "'";
        return false;
    }
    std::ostringstream all;
    all << file.rdbuf();
    outText = all.str();
    return true;
}

bool WriteWholeFile(const fs::path& path, std::string_view text, std::string& outError) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    // Truncating, not appending: this writes the whole file every time.
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        outError = "cannot write '" + path.string() + "'";
        return false;
    }
    file << text;
    if (!file) {
        outError = "writing '" + path.string() + "' failed part way through";
        return false;
    }
    return true;
}

// The folder's own name, for a project whose manifest does not give one.
//
// filename() rather than stem(), and a parent_path() step for the case of a
// path that ends in a separator - `D:/games/orbit/` has an empty filename()
// and would otherwise produce a nameless project.
std::string FolderName(const fs::path& directory) {
    fs::path trimmed = directory;
    if (trimmed.filename().empty()) {
        trimmed = trimmed.parent_path();
    }
    const std::string name = trimmed.filename().string();

    // A drive root - `D:/` - has no filename at all. Nothing better to say.
    return name.empty() ? trimmed.string() : name;
}

// An absolute path with no trailing separator, which is what ProjectInfo
// promises and what comparing two roots for equality needs.
std::string NormalisedDirectory(const fs::path& directory) {
    std::error_code ec;
    fs::path        absolute = fs::absolute(directory, ec);
    if (ec) {
        absolute = directory;   // unreachable drive; say what we were given
    }
    absolute = absolute.lexically_normal();
    if (absolute.filename().empty() && absolute.has_parent_path()) {
        absolute = absolute.parent_path();
    }
    return absolute.generic_string();
}

// The keys this version writes itself. Everything else found in a manifest is
// somebody else's and is kept - see ProjectInfo::unknown.
bool IsKnownKey(const std::string& key) {
    return key == "_comment" || key == "formatVersion" || key == "name" ||
           key == "engineVersion" || key == "addons";
}

constexpr const char* kManifestComment =
    "This folder is an Engine2D project. The editor is pointed at it with "
    "'editor --project <this folder>'. Deleting this file does not break the "
    "project - a folder with an assets/ folder still opens - but then it has no "
    "name of its own. See engine/include/engine/project/Project.h.";

} // namespace

// ===========================================================================
//  Looks - the one definition of "is this folder a project".
//
//  FileSystem::LooksLikeRoot calls this, so the engine and the editor and the
//  Hub cannot come to different conclusions about the same folder.
// ===========================================================================
bool Project::Looks(std::string_view realDirectory) {
    if (realDirectory.empty()) {
        return false;
    }

    const fs::path  directory(realDirectory);
    std::error_code ec;

    // A manifest is the explicit answer.
    if (fs::is_regular_file(directory / kFileName, ec)) {
        return true;
    }

    // And an assets/ folder is the historical one. See the header.
    return fs::is_directory(directory / "assets", ec);
}

bool Project::Read(std::string_view realDirectory, ProjectInfo& out,
                   std::string& outError) {
    out = ProjectInfo{};

    if (!Looks(realDirectory)) {
        outError = "'" + std::string(realDirectory) +
                   "' is not a project: it has no " + kFileName +
                   " and no assets folder";
        return false;
    }

    const fs::path directory(realDirectory);
    out.realRoot = NormalisedDirectory(directory);
    out.name     = FolderName(directory);

    const fs::path  manifest = directory / kFileName;
    std::error_code ec;
    if (!fs::is_regular_file(manifest, ec)) {
        // A project with no manifest. NOT an error - see the header note on
        // backward compatibility. The caller says so once and offers to write
        // one; it does not write one by itself.
        return true;
    }

    std::string text;
    if (!ReadWholeFile(manifest, text, outError)) {
        // The file is there and will not open - a permissions problem, or a
        // folder by that name. Worth a warning, not worth refusing to open a
        // project whose content is all perfectly readable.
        ENGINE_LOG_WARN(Channels::kCore,
                        "{} could not be read ({}), so '{}' is open under its folder "
                        "name",
                        kFileName, outError, out.name);
        outError.clear();
        return true;
    }

    std::string parseError;
    const Json  document = ParseJson(text, parseError);
    if (!parseError.empty() || !document.is_object()) {
        ENGINE_LOG_WARN(Channels::kCore,
                        "{} is not valid JSON ({}), so '{}' is open under its folder "
                        "name. Fix the file or delete it - the project works either way.",
                        kFileName, parseError.empty() ? "it is not an object" : parseError,
                        out.name);
        return true;
    }

    out.hasManifest = true;

    // A FILE FROM A NEWER EDITOR STILL OPENS. Both numbers are named so the
    // message is actionable rather than just alarming.
    const int formatVersion = ReadInt(document, "formatVersion", kFormatVersion, kFileName);
    if (formatVersion > kFormatVersion) {
        ENGINE_LOG_WARN(Channels::kCore,
                        "'{}' was last opened by a newer editor ({} understands format {}, "
                        "the file says {}). It will open, but anything that editor added "
                        "may not be shown.",
                        out.name, "this build", kFormatVersion, formatVersion);
    }

    // An empty or missing name falls back to the folder, so a hand-written
    // manifest containing nothing but `{}` is still valid.
    const std::string named = ReadString(document, "name", "", kFileName);
    if (!named.empty()) {
        out.name = named;
    }

    out.engineVersion = ReadString(document, "engineVersion", "", kFileName);

    if (HasKey(document, "addons") && document["addons"].is_array()) {
        for (const Json& entry : document["addons"]) {
            if (entry.is_string()) {
                out.addons.push_back(entry.get<std::string>());
            }
        }
    }

    // Anything this build does not know about is carried so Write can put it
    // back. See ProjectInfo::unknown.
    out.unknown = Json::object();
    for (auto it = document.begin(); it != document.end(); ++it) {
        if (!IsKnownKey(it.key())) {
            out.unknown[it.key()] = it.value();
        }
    }

    return true;
}

bool Project::Write(ProjectInfo& info, std::string& outError) {
    if (info.realRoot.empty()) {
        outError = "no project folder to write to";
        return false;
    }

    // Unknown keys FIRST, so that a key this build does know about always
    // wins if it somehow appears in both. The alternative silently lets a
    // stale copy of `name` overwrite the real one.
    Json document = info.unknown.is_object() ? info.unknown : Json::object();

    document["_comment"]     = kManifestComment;
    document["formatVersion"] = kFormatVersion;
    document["name"]          = info.name;
    document["engineVersion"] = info.engineVersion;
    document["addons"]        = info.addons;

    const fs::path manifest = fs::path(info.realRoot) / kFileName;
    if (!WriteWholeFile(manifest, document.dump(2) + "\n", outError)) {
        return false;
    }

    info.hasManifest = true;
    return true;
}

std::string Project::FindAbove(std::string_view realStartDirectory) {
    if (realStartDirectory.empty()) {
        return {};
    }

    std::error_code ec;
    fs::path        current = fs::absolute(fs::path(realStartDirectory), ec);
    if (ec) {
        return {};
    }
    current = current.lexically_normal();

    // The same bounded walk FileSystem has always used, and bounded for the
    // same reason: a stop so that a bad starting point cannot walk forever.
    // Twelve is far more than any real layout and far less than forever.
    for (int step = 0; step < 12; ++step) {
        if (Looks(current.generic_string())) {
            return NormalisedDirectory(current);
        }
        if (!current.has_parent_path() || current.parent_path() == current) {
            break;   // the top of the drive
        }
        current = current.parent_path();
    }

    return {};
}

bool Project::NameIsUsable(std::string_view name, std::string& outError) {
    if (name.empty()) {
        outError = "a project needs a name";
        return false;
    }

    // A name becomes a FOLDER name, so anything that would turn one folder
    // into two - or into a different one entirely - has to go. Checked
    // explicitly rather than by sanitising the name, because quietly renaming
    // what somebody typed is worse than telling them.
    if (name.find('/') != std::string_view::npos ||
        name.find('\\') != std::string_view::npos) {
        outError = "a project name cannot contain a path separator";
        return false;
    }
    if (name == "." || name == "..") {
        outError = "'" + std::string(name) + "' is not a usable project name";
        return false;
    }

    // Windows refuses these outright and silently mangles trailing dots and
    // spaces. Rejected everywhere rather than only on Windows, so that a
    // project made on one machine opens on another.
    constexpr std::string_view kForbidden = "<>:\"|?*";
    if (name.find_first_of(kForbidden) != std::string_view::npos) {
        outError = "a project name cannot contain any of " + std::string(kForbidden);
        return false;
    }
    if (name.back() == '.' || name.back() == ' ') {
        outError = "a project name cannot end with a dot or a space";
        return false;
    }

    return true;
}

bool Project::Create(std::string_view realParentDirectory, std::string_view name,
                     ProjectInfo& out, std::string& outError) {
    out = ProjectInfo{};

    if (!NameIsUsable(name, outError)) {
        return false;
    }
    if (realParentDirectory.empty()) {
        outError = "no folder to create the project in";
        return false;
    }

    std::error_code ec;
    const fs::path  parent = fs::absolute(fs::path(realParentDirectory), ec);
    if (ec) {
        outError = "'" + std::string(realParentDirectory) + "' cannot be reached";
        return false;
    }
    if (!fs::is_directory(parent, ec)) {
        outError = "'" + parent.string() + "' is not a folder";
        return false;
    }

    // A PROJECT INSIDE A PROJECT IS REFUSED. Two manifests above one assets/
    // folder gives the upward walk two valid answers, and nothing to choose
    // between them with.
    if (const std::string enclosing = FindAbove(parent.generic_string());
        !enclosing.empty()) {
        outError = "'" + parent.string() + "' is already inside the project at '" +
                   enclosing + "'. A project cannot contain another one.";
        return false;
    }

    const fs::path root = parent / fs::path(std::string(name));

    // An existing EMPTY folder is fine - picking the folder you just made in
    // a file dialog is the obvious way to do this. An existing non-empty one
    // is refused rather than merged into.
    if (fs::exists(root, ec)) {
        if (!fs::is_directory(root, ec)) {
            outError = "'" + root.string() + "' already exists and is not a folder";
            return false;
        }
        if (!fs::is_empty(root, ec)) {
            outError = "'" + root.string() + "' already exists and is not empty";
            return false;
        }
    }

    // ---- the skeleton -----------------------------------------------------
    //
    // assets/ is what makes it a project even without the manifest, and the
    // three folders inside it are the ones the editor's own panels expect to
    // find. Created empty: there is no starter content, for the reason given
    // in the header.
    for (const char* folder : {"assets/scenes", "assets/scripts", "assets/textures"}) {
        fs::create_directories(root / folder, ec);
        if (ec) {
            outError = "could not create '" + (root / folder).string() + "': " +
                       ec.message();
            return false;
        }
    }

    // ---- config/engine.json ----------------------------------------------
    //
    // Written as text rather than built with nlohmann so that the comments
    // survive. A new project's settings file is the first thing somebody
    // opens to change something, and a file explaining itself is worth more
    // than one that round-trips perfectly.
    //
    // `startup.scene` is deliberately empty - see the header. The key is
    // present rather than absent so that it is obvious where the scene name
    // goes.
    std::ostringstream config;
    config << "{\n"
           << "  \"_comment\": \"The settings this project's engine reads when it "
              "starts. Change a value, save, and run again - no rebuilding. The "
              "documented copy of every setting is config/engine.example.json in the "
              "engine's own source tree.\",\n"
           << "\n"
           << "  \"window\": {\n"
           << "    \"title\": \"" << name << "\",\n"
           << "    \"width\": 1280,\n"
           << "    \"height\": 720\n"
           << "  },\n"
           << "\n"
           << "  \"logging\": {\n"
           << "    \"_note\": \"threshold hides anything less important than the level "
              "you name. Info, Warning or Error.\",\n"
           << "    \"threshold\": \"Info\",\n"
           << "    \"file\": \"logs/engine.log\"\n"
           << "  },\n"
           << "\n"
           << "  \"startup\": {\n"
           << "    \"_note\": \"Which scene opens with the project. Empty means no "
              "scene - use Scene -> New Scene in the editor to make the first one.\",\n"
           << "    \"scene\": \"\"\n"
           << "  }\n"
           << "}\n";
    if (!WriteWholeFile(root / "config" / "engine.json", config.str(), outError)) {
        return false;
    }

    // ---- .gitignore -------------------------------------------------------
    //
    // Written because a new project is very likely about to become a git
    // repository, and the three folders below are the ones that must not be
    // in it: generated, machine-specific, and in .build/'s case enormous.
    // Getting this wrong is only noticed after it has been committed.
    std::ostringstream ignore;
    ignore << "# What the engine generates. None of it belongs in version control:\n"
           << "# it is rebuilt from assets/ and config/ on the next run.\n"
           << "\n"
           << "# Compiled scripts - one library per script, plus the objects and the\n"
           << "# precompiled header they are built from.\n"
           << ".build/\n"
           << "\n"
           << "# The run log.\n"
           << "logs/\n"
           << "\n"
           << "# Where the editor remembers your place: the open scene, the selection,\n"
           << "# the Scene view's camera and the panel layout. One person's place in\n"
           << "# the work, so committing it would move everybody else's camera.\n"
           << ".editor/\n";
    if (!WriteWholeFile(root / ".gitignore", ignore.str(), outError)) {
        return false;
    }

    // ---- the manifest, last ----------------------------------------------
    //
    // Last on purpose: it is what Looks() keys on, so a half-made project
    // that failed above this line does not look like a finished one.
    out.realRoot = NormalisedDirectory(root);
    out.name     = std::string(name);
    if (!Write(out, outError)) {
        return false;
    }

    ENGINE_LOG_INFO(Channels::kCore, "created the project '{}' in '{}'", out.name,
                    out.realRoot);
    return true;
}

} // namespace eng
