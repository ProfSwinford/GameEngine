// ============================================================================
//  GameExport.cpp - the copy that makes a standalone game. See GameExport.h.
// ============================================================================

#include "GameExport.h"

#include "ScriptBuild.h"

#include <engine/Engine.h>
#include <engine/platform/FolderDialog.h>
#include <engine/core/Json.h>
#include <engine/core/Log.h>
#include <engine/fs/AssetArchive.h>
#include <engine/fs/FileSystem.h>
#include <engine/platform/Window.h>
#include <engine/scene/Scene.h>
#include <engine/scene/ScriptLibrary.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace editor {
namespace {

namespace fs = std::filesystem;

// Which question this file asks the shared folder picker. The project menu
// uses its own numbers; they only have to differ. See FolderDialog.h.
constexpr int kPurposeExport = 10;



// The folder the editor itself is running from, which is where player.exe,
// engine.dll and SDL3.dll sit. SDL_GetBasePath hands back a string IT owns, so
// it is copied rather than kept.
fs::path EditorDirectory() {
    const char* base = SDL_GetBasePath();
    return base != nullptr ? fs::path(base) : fs::current_path();
}

// Is `candidate` the same folder as `other`, or inside it?
//
// Used to refuse exporting into the project itself. Without this check,
// exporting into assets/ would copy assets/ into a folder inside assets/,
// which the copy would then find and copy again - and the first sign of
// trouble would be the disk filling up.
bool IsSameOrInside(const fs::path& candidate, const fs::path& other) {
    std::error_code ec;
    const fs::path  a = fs::weakly_canonical(candidate, ec);
    const fs::path  b = fs::weakly_canonical(other, ec);
    if (ec) {
        return false;
    }
    auto ia = a.begin();
    auto ib = b.begin();
    for (; ib != b.end(); ++ia, ++ib) {
        if (ia == a.end() || *ia != *ib) {
            return false;
        }
    }
    return true;
}

bool IsScriptSource(const fs::path& path) {
    const std::string ext = path.extension().string();
    return ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".cc" || ext == ".cxx";
}

// Makes a path Windows will accept however long it is.
//
// WINDOWS REFUSES PATHS OVER 260 CHARACTERS through the ordinary file API, and
// the failure is reported as "The system cannot find the path specified" -
// which sends you looking for a missing folder that is right there. An asset
// pack a few folders deep plus a destination a few folders deep reaches 260
// easily; this was found by exporting into a long temporary path and watching
// a hundred perfectly good .png files fail to copy.
//
// The \\?\ prefix turns off that parsing in the Win32 layer. It only works on
// a fully qualified path with no . or .. left in it, which is what absolute()
// and lexically_normal() are for, and only for drive-letter paths - a UNC
// share needs a different prefix and is left alone.
//
// The prefix is UGLY, so it is applied only where the path is handed to the
// filesystem. Anything shown to a person uses the plain version.
fs::path LongPathSafe(const fs::path& path) {
#if defined(_WIN32)
    std::error_code ec;
    const fs::path  full = fs::absolute(path, ec);
    if (ec) {
        return path;
    }
    const std::wstring text = full.lexically_normal().make_preferred().wstring();
    if (text.rfind(LR"(\\?\)", 0) == 0) {
        return fs::path(text);   // already prefixed
    }
    if (text.size() > 1 && text[1] == L':') {
        return fs::path(LR"(\\?\)" + text);
    }
    return fs::path(text);
#else
    return path;
#endif
}

// Copies one file, making the parent folder first. Counts rather than throws,
// so one unreadable file does not abandon the export half done.
bool CopyOneFile(const fs::path& from, const fs::path& to, std::string& outError) {
    std::error_code ec;
    const fs::path  target = LongPathSafe(to);
    fs::create_directories(target.parent_path(), ec);
    fs::copy_file(from, target, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        outError = ec.message();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  WHICH ASSETS ARE ACTUALLY USED
//
//  A game ships the files it refers to, and nothing else. Being in assets/ is
//  not a reason to ship something: a project picks up reference art, asset
//  packs bought for a later level, half-finished sprites and whatever was
//  being tried out last week, and all of that would otherwise go out to
//  players inside a folder they can simply open.
//
//  It is not only size. Shipping an unreferenced folder is how unannounced
//  content, licensed art you have no right to redistribute, and the C# source
//  of an asset pack end up in somebody's hands.
//
//  HOW A REFERENCE IS FOUND: every string in every scene file is tested
//  against the disk. If "textures/player.png" names a real file under assets/,
//  it is an asset this game uses.
//
//  That is deliberately a DUMB test, and it is the right one here. It needs no
//  list of which component fields hold paths, so it already works for the
//  sprite-sheet and animation fields that do not exist yet - the day a
//  component starts referring to a file, the exporter packs it with no edit.
//  The cost is that a string which happens to match a filename packs one
//  harmless extra file.
//
//  WHAT IT CANNOT SEE is a path a SCRIPT builds while running -
//  ResourceManager::LoadTexture("textures/" + name). Nothing can, short of
//  running the game. assets/export.json is the answer to that; see below.
// ---------------------------------------------------------------------------

// Calls `fn` for every string anywhere in a JSON document, at any depth.
void ForEachString(const eng::Json& node, const std::function<void(const std::string&)>& fn) {
    if (node.is_string()) {
        fn(node.get<std::string>());
        return;
    }
    if (node.is_object() || node.is_array()) {
        for (const auto& child : node) {
            ForEachString(child, fn);
        }
    }
}

// Does this virtual path name a real file INSIDE assets/?
//
// The containment check is not paranoia for its own sake. A scene file is
// data, and a string in it like "../../../config/engine.json" or something
// further afield would otherwise be resolved and packed - so a hand-edited
// scene could pull a file from anywhere on the machine into the exported
// folder. The exporter decides what leaves this computer, so it checks.
bool IsAssetUnderAssets(const std::string& virtualPath, const fs::path& assetsDir) {
    if (virtualPath.empty()) {
        return false;
    }
    std::error_code ec;
    const fs::path  resolved = fs::path(eng::FileSystem::Resolve(virtualPath));
    if (!fs::is_regular_file(resolved, ec)) {
        return false;
    }
    return IsSameOrInside(resolved, assetsDir);
}

// Adds one file, or every file under one folder, to the pack list.
void AddPath(const std::string& virtualPath, const fs::path& assetsDir,
             std::set<std::string>& out) {
    std::error_code ec;
    const fs::path  resolved = fs::path(eng::FileSystem::Resolve(virtualPath));

    if (fs::is_regular_file(resolved, ec)) {
        if (IsSameOrInside(resolved, assetsDir)) {
            out.insert(virtualPath);
        }
        return;
    }

    if (!fs::is_directory(resolved, ec) || !IsSameOrInside(resolved, assetsDir)) {
        return;
    }

    // A folder means "all of it", which is what somebody naming a folder in
    // export.json means. Script source is still left behind.
    for (fs::recursive_directory_iterator it(resolved, ec), end; it != end;
         it.increment(ec)) {
        if (ec) {
            break;
        }
        const std::string name = it->path().filename().string();
        if (it->is_directory(ec) && !name.empty() && name[0] == '.') {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec) || IsScriptSource(it->path())) {
            continue;
        }
        const fs::path relative = fs::relative(it->path(), assetsDir, ec);
        if (!ec) {
            out.insert(relative.generic_string());
        }
    }
}

// Anything the scenes cannot reveal: assets a script loads by a name it builds
// at run time. Entirely optional - a project with no export.json is normal.
//
//     { "include": ["textures/ui", "textures/boss_reveal.png"] }
//
// Each entry is a virtual path. A file packs that file; a folder packs
// everything in it.
void AddManifestIncludes(const fs::path& assetsDir, std::set<std::string>& out,
                         int& outCount) {
    const std::string manifestVirtual = "export.json";
    std::error_code   ec;
    if (!fs::is_regular_file(fs::path(eng::FileSystem::Resolve(manifestVirtual)), ec)) {
        return;
    }

    std::string text;
    std::string error;
    if (!eng::FileSystem::ReadTextFile(manifestVirtual, text, error)) {
        ENGINE_LOG_WARN(eng::Channels::kEditor, "could not read assets/export.json: {}",
                        error);
        return;
    }

    const eng::Json document = eng::ParseJson(text, error);
    if (!error.empty()) {
        ENGINE_LOG_WARN(eng::Channels::kEditor,
                        "assets/export.json is not valid JSON ({}), so nothing extra "
                        "was packed from it",
                        error);
        return;
    }

    const auto includes = document.find("include");
    if (includes == document.end() || !includes->is_array()) {
        return;
    }

    const std::size_t before = out.size();
    for (const auto& entry : *includes) {
        if (!entry.is_string()) {
            continue;
        }
        AddPath(entry.get<std::string>(), assetsDir, out);
    }
    outCount = static_cast<int>(out.size() - before);
}

// The set of virtual paths this game actually needs.
std::set<std::string> CollectReferencedAssets(const fs::path& assetsDir, int& outScenes,
                                              int& outFromManifest) {
    std::set<std::string> needed;

    // THE PLACEHOLDER IS ALWAYS PACKED. Nothing refers to it from any scene -
    // the engine loads textures/missing.bmp by name when a sprite's own image
    // will not load. Leave it out and the one thing whose job is to make a
    // failure visible becomes a failure of its own.
    AddPath("textures/missing.bmp", assetsDir, needed);

    // THE BUILD LIST DECIDES WHAT SHIPS - see scene/BuildScenes.h and the
    // editor's Build panel.
    //
    // Not every scene in the folder, which is what this used to do. A project
    // collects test levels, a scratch scene somebody was debugging in, and
    // last week's copy of level 3, and none of that belongs in front of a
    // player. The list is also what gives each scene its NUMBER, so it has to
    // be the thing the export follows or the numbers in the shipped game would
    // not match the numbers in the editor.
    //
    // The list itself is packed too: the game reads it at startup to find out
    // which scene is scene 0.
    // READ FROM DISK HERE, not inherited from whoever started up.
    //
    // Engine::Init loads the list, but `editor --build-game` never calls
    // Engine::Init - it brings up the log and the file system and nothing
    // else - so relying on that left the export thinking every project had an
    // empty list and silently packing everything. Reading it here also means
    // the export uses what was SAVED, which is what the Build panel's Save
    // button promises.
    std::string buildListError;
    eng::BuildScenes::Load(buildListError);

    std::vector<std::string> scenes = eng::BuildScenes::All();

    if (scenes.empty()) {
        // FALLING BACK RATHER THAN SHIPPING NOTHING. A project that has never
        // opened the Build panel has no list, and silently producing a game
        // with no scenes in it would be a dreadful way to find that out.
        if (!eng::FileSystem::ListFiles("scenes", eng::kSceneExtension, scenes)) {
            ENGINE_LOG_WARN(eng::Channels::kEditor, "could not read assets/scenes/");
        }
        ENGINE_LOG_WARN(eng::Channels::kEditor,
                        "the build list is empty, so all {} scene(s) in assets/scenes/ "
                        "were packed. Use the Build panel to choose which ship and "
                        "which one starts the game.",
                        scenes.size());
    } else {
        needed.insert(eng::BuildScenes::kVirtualPath);
    }

    outScenes = static_cast<int>(scenes.size());

    for (const std::string& scene : scenes) {
        needed.insert(scene);

        std::string text;
        std::string error;
        if (!eng::FileSystem::ReadTextFile(scene, text, error)) {
            ENGINE_LOG_WARN(eng::Channels::kEditor, "could not read '{}': {}", scene,
                            error);
            continue;
        }
        const eng::Json document = eng::ParseJson(text, error);
        if (!error.empty()) {
            ENGINE_LOG_WARN(eng::Channels::kEditor,
                            "'{}' is not valid JSON ({}), so the assets it refers to "
                            "could not be found",
                            scene, error);
            continue;
        }

        ForEachString(document, [&](const std::string& value) {
            if (IsAssetUnderAssets(value, assetsDir)) {
                needed.insert(value);
            }
        });
    }

    // ---- assets that refer to further assets -------------------------------
    //
    // A scene names "tiles/grass.otile". THAT file names the sheet it draws
    // from, and may name other .otile files it connects to - and nothing above
    // reads it, because only scenes are walked. Without this an exported game
    // ships every tile definition and none of their pictures, and the whole
    // level comes out as magenta placeholder squares.
    //
    // A WORKLIST rather than a second pass, because these files can refer to
    // each other: following one can reveal another. `needed` is a set and only
    // new entries are queued, so two tiles that name each other terminate by
    // themselves.
    //
    // Only .otile is followed, deliberately. The scene walk above is a dumb
    // "does this string name a real file" test precisely so it needs no list
    // of which fields hold paths; this is the narrow exception for an asset
    // that is itself a document, and it should stay narrow.
    std::vector<std::string> toFollow(needed.begin(), needed.end());
    while (!toFollow.empty()) {
        const std::string path = std::move(toFollow.back());
        toFollow.pop_back();

        if (!eng::FileSystem::HasExtension(path, ".otile")) {
            continue;
        }

        std::string text;
        std::string error;
        if (!eng::FileSystem::ReadTextFile(path, text, error)) {
            ENGINE_LOG_WARN(eng::Channels::kEditor, "could not read '{}': {}", path,
                            error);
            continue;
        }
        const eng::Json document = eng::ParseJson(text, error);
        if (!error.empty()) {
            ENGINE_LOG_WARN(eng::Channels::kEditor,
                            "'{}' is not valid JSON ({}), so its tile sheet could not "
                            "be found and the exported game would draw it as the "
                            "magenta placeholder",
                            path, error);
            continue;
        }

        ForEachString(document, [&](const std::string& value) {
            if (IsAssetUnderAssets(value, assetsDir) && needed.insert(value).second) {
                toFollow.push_back(value);
            }
        });
    }

    AddManifestIncludes(assetsDir, needed, outFromManifest);
    return needed;
}

// How many files are under assets/, and how big - for the line that says what
// was LEFT BEHIND, which is the number that proves this works.
void MeasureAssets(const fs::path& assetsDir, int& outFiles, std::uintmax_t& outBytes) {
    outFiles = 0;
    outBytes = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(assetsDir, ec), end; it != end;
         it.increment(ec)) {
        if (ec) {
            break;
        }
        const std::string name = it->path().filename().string();
        if (it->is_directory(ec) && !name.empty() && name[0] == '.') {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) {
            continue;
        }
        ++outFiles;
        outBytes += fs::file_size(it->path(), ec);
    }
}

} // namespace

void GameExport::AskForDestination() {
    // DELEGATED. The picker and - far more importantly - the rule about
    // which thread its answer arrives on now live in the engine, because
    // opening and creating a project need the same thing. Two copies of a
    // threading contract is one copy that can be fixed while the other is
    // not. See engine/include/engine/platform/FolderDialog.h.
    eng::FolderDialog::Ask(kPurposeExport);
}

GameExport::Pending GameExport::TakePending() {
    // Only answers questions THIS asked. There is one dialog and one
    // pending answer, so without the purpose check, picking a folder for
    // Open Project would be collected here and exported into.
    const eng::FolderDialog::Result picked = eng::FolderDialog::Take();
    if (picked.purpose != kPurposeExport) {
        return Pending{};
    }

    Pending out;
    out.path  = picked.path;
    out.error = picked.error;
    switch (picked.choice) {
        case eng::FolderDialog::Choice::Chosen:    out.choice = Choice::Chosen;    break;
        case eng::FolderDialog::Choice::Cancelled: out.choice = Choice::Cancelled; break;
        case eng::FolderDialog::Choice::Failed:    out.choice = Choice::Failed;    break;
        case eng::FolderDialog::Choice::None:      out.choice = Choice::None;      break;
    }
    return out;
}

GameExport::Result GameExport::ExportTo(const std::string& destination) {
    Result result;

    if (destination.empty()) {
        result.summary = "no folder was chosen";
        return result;
    }

    const fs::path dest      = fs::path(destination);
    const fs::path root      = fs::path(eng::FileSystem::AssetRoot());
    const fs::path assetsDir = fs::path(eng::FileSystem::Resolve(""));

    // REFUSED rather than attempted. See IsSameOrInside.
    if (IsSameOrInside(dest, root)) {
        result.summary = "cannot export into the project folder itself";
        ENGINE_LOG_ERROR(eng::Channels::kEditor,
                         "refusing to export into '{}': it is the project folder, or "
                         "inside it, and the copy would feed on itself",
                         dest.string());
        return result;
    }

    std::error_code ec;
    fs::create_directories(dest, ec);
    if (ec) {
        result.summary = "could not create that folder";
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not create '{}': {}",
                         dest.string(), ec.message());
        return result;
    }

    // ---- 1. the scripts ---------------------------------------------------
    //
    // Built FIRST, and the export stops if they do not compile. Shipping a
    // game whose scripts failed to build would produce a folder that runs,
    // opens a window, and does nothing - which is far worse than being told
    // the build failed.
    // ONLY WHEN THEY ARE ACTUALLY OUT OF DATE.
    //
    // Compiling the scripts takes something like thirty seconds, and the
    // editor is one thread - so it does not draw, does not respond and does
    // not update the status bar for the whole of it. Doing that on every
    // export, including the common one where nothing has changed since the
    // last build, is how a working feature comes to look like a broken one:
    // you press Build, the window stops responding, and the folder you are
    // staring at is still empty because the copy has not started yet.
    //
    // NeedsRebuild is the same check RebuildScriptsIfChanged uses, so the
    // library is current for exactly the same reasons it is current when you
    // alt-tab back into the editor.
    // ALWAYS, and into ONE library rather than the per-script ones the editor
    // runs on. A shipped game will never recompile a script, so eleven
    // libraries would be eleven import tables and eleven lots of load-time
    // relocation for nothing. See ScriptBuild::BuildBundle - it links the
    // objects the editor already compiled, so this is a link and not a
    // rebuild.
    //
    // Not guarded by NeedsRebuild any more: the bundle is a different output
    // from the modules, so "the modules are up to date" says nothing about
    // whether it exists. BuildBundle is incremental by itself.
    {
        const ScriptBuild::Result scripts = ScriptBuild::BuildBundle();
        if (!scripts.ok) {
            result.summary = "the scripts did not build - the game was not exported";
            ENGINE_LOG_ERROR(
                eng::Channels::kEditor,
                "export stopped: {}. The compiler's output is in the Console.",
                scripts.summary);
            return result;
        }
    }

    // ---- 2. the executable and the libraries ------------------------------
    const fs::path editorDir = EditorDirectory();

    // Named after the folder the user chose, so exporting to "MyGame" gives
    // them MyGame.exe rather than something called "player".
    std::string gameName = dest.filename().string();
    if (gameName.empty()) {
        gameName = "Game";   // they picked a drive root; not worth refusing over
    }

    struct Binary {
        fs::path    from;
        std::string to;
        bool        required;
    };
    const std::vector<Binary> binaries{
        {editorDir / "player.exe", gameName + ".exe", true},
        {editorDir / "engine.dll", "engine.dll", true},
        {editorDir / "SDL3.dll", "SDL3.dll", true},
    };

    int filesCopied = 0;
    for (const Binary& binary : binaries) {
        if (!fs::exists(binary.from, ec)) {
            if (binary.required) {
                result.summary = binary.from.filename().string() + " is missing";
                ENGINE_LOG_ERROR(eng::Channels::kEditor,
                                 "cannot export: '{}' is not beside the editor. Build "
                                 "the 'player' target and try again.",
                                 binary.from.string());
                return result;
            }
            continue;
        }
        std::string error;
        if (CopyOneFile(binary.from, dest / binary.to, error)) {
            ++filesCopied;
        } else {
            result.summary = "could not copy " + binary.to;
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not copy '{}': {}",
                             binary.to, error);
            return result;
        }
    }

    // ---- 3. the compiled scripts -----------------------------------------
    //
    // FROM .build/, TO assets/. The two are deliberately different places.
    //
    // The editor compiles into .build/ with the object files; a shipped game
    // keeps the result in assets/ with the scenes and textures. A hidden
    // folder called .build sitting beside a game's executable reads as
    // leftover build rubbish, and on Linux and macOS the person who unzipped
    // the game cannot even see it. ScriptLibrary::FindLibraryPath is what
    // knows about both. See ScriptLibrary.h.
    const fs::path libraryFrom =
        fs::path(eng::FileSystem::Resolve(eng::ScriptLibrary::DefaultVirtualPath()));
    const std::string libraryVirtual = eng::ScriptLibrary::ShippedVirtualPath();
    if (fs::exists(libraryFrom, ec)) {
        std::string error;
        if (CopyOneFile(libraryFrom, dest / "assets" / libraryVirtual, error)) {
            ++filesCopied;
        } else {
            ENGINE_LOG_WARN(eng::Channels::kEditor,
                            "could not copy the script library: {}", error);
        }
    } else {
        // Not fatal: a project with no scripts at all is a legitimate thing to
        // export, and the engine treats a missing library as "no scripts".
        ENGINE_LOG_WARN(eng::Channels::kEditor,
                        "there is no script library to copy, so the exported game will "
                        "have no scripted behaviour");
    }

    // ---- 4. the settings --------------------------------------------------
    //
    // NOT COPIED LOOSE ANY MORE. config/engine.json names the starting scene,
    // so leaving it beside the executable is leaving an invitation to change
    // which level the game opens with - and to raise the log threshold, and to
    // point the window wherever. It is packed into scenes.bin instead, with
    // the scenes it chooses between; the entry is added to `needed` here and
    // written in step 5.
    //
    // The lookup that finds it there is explained in AssetArchive.cpp's
    // FindEntry: an archive may hold an entry under a full virtual path, which
    // is how one file outside assets/ travels inside scenes.bin without
    // needing a config.bin of its own.
    const fs::path configFrom = fs::path(eng::FileSystem::Resolve("config/engine.json"));
    const bool     packConfig = fs::exists(configFrom, ec);
    if (!packConfig) {
        ENGINE_LOG_WARN(eng::Channels::kEditor,
                        "there is no config/engine.json, so the game will start with "
                        "built-in defaults");
    }

    // ---- 5. the assets THE GAME ACTUALLY USES -----------------------------
    //
    // Referenced, not everything present. See the long note above
    // CollectReferencedAssets for why that distinction is the whole point of
    // this step.
    int                   sceneCount    = 0;
    int                   manifestCount = 0;
    std::set<std::string> needed = CollectReferencedAssets(assetsDir, sceneCount,
                                                           manifestCount);

    // The settings join the scenes, for the reason in step 4. This is the one
    // packed entry that does not live under assets/, so it is the one that
    // needs FindEntry's second rule to be found again at run time.
    if (packConfig) {
        needed.insert("config/engine.json");
    }

    // ONE .bin PER TOP-LEVEL FOLDER, not loose files.
    //
    // assets/scenes/* becomes assets/scenes.bin, assets/textures/* becomes
    // assets/textures.bin. A player who opens the exported game finds two
    // files they cannot usefully edit instead of a folder of .png and .oscene
    // inviting them to repaint the sprites and give themselves a hundred
    // lives. See engine/include/engine/fs/AssetArchive.h - including its note
    // about what this is and is not.
    //
    // THE ARCHIVES GO INSIDE assets/, and that is not cosmetic: FileSystem
    // finds the game's root by walking up from the executable looking for a
    // folder literally called `assets`. Put the .bin files beside the exe
    // instead and the game finds no root, loads nothing, and opens an empty
    // window.
    //
    // Grouped by the FIRST path segment, which is the folder the archive
    // stands in for. A file sitting directly in assets/ with no folder of its
    // own belongs to no archive, so it is copied loose - there is nothing to
    // pack it into, and inventing one would mean inventing a name.
    std::map<std::string, std::vector<eng::AssetArchive::SourceFile>> byFolder;
    std::vector<std::string>                                         loose;

    for (const std::string& virtualPath : needed) {
        eng::AssetArchive::SourceFile file;
        file.sourcePath = eng::FileSystem::Resolve(virtualPath);

        // THE SETTINGS RIDE ALONG IN scenes.bin, under their FULL path.
        //
        // config/engine.json is not under assets/, so the usual rule - first
        // segment names the archive - would ask for a config.bin holding one
        // file. Instead it goes in beside the scenes it chooses between, keyed
        // by the whole path, which is exactly the case AssetArchive's FindEntry
        // second rule exists to answer.
        // Every packed file is named here, where the ORIGINAL virtual path is
        // still in hand. Naming it later, from the archive entry, printed
        // "scenes/config/engine.json" for the settings - an entry name glued
        // onto an archive name, describing a path that does not exist.
        ENGINE_LOG_INFO(eng::Channels::kEditor, "  packed {}", virtualPath);

        if (virtualPath.starts_with("config/")) {
            file.entryName = virtualPath;
            byFolder["scenes"].push_back(std::move(file));
            continue;
        }

        const std::size_t slash = virtualPath.find('/');
        if (slash == std::string::npos || slash == 0 || slash + 1 >= virtualPath.size()) {
            loose.push_back(virtualPath);
            continue;
        }
        file.entryName = virtualPath.substr(slash + 1);
        byFolder[virtualPath.substr(0, slash)].push_back(std::move(file));
    }

    int assetsCopied = 0;
    for (const auto& [folder, contents] : byFolder) {
        const fs::path archivePath =
            dest / "assets" / (folder + eng::AssetArchive::kExtension);

        std::string error;
        if (!eng::AssetArchive::Write(archivePath.string(), contents, error)) {
            result.summary = "could not write " + folder + eng::AssetArchive::kExtension;
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not write '{}': {}",
                             archivePath.string(), error);
            return result;
        }

        assetsCopied += static_cast<int>(contents.size());
        ++filesCopied;   // the archive itself is one file in the shipped folder

        ENGINE_LOG_INFO(eng::Channels::kEditor, "  -> {}{} ({} file(s))", folder,
                        eng::AssetArchive::kExtension, contents.size());
    }

    for (const std::string& virtualPath : loose) {
        const fs::path from = fs::path(eng::FileSystem::Resolve(virtualPath));
        std::string    error;
        if (CopyOneFile(from, dest / "assets" / virtualPath, error)) {
            ++assetsCopied;
            ++filesCopied;
            ENGINE_LOG_INFO(eng::Channels::kEditor,
                            "  packed {} (loose - it is not inside a folder)",
                            virtualPath);
        } else {
            ENGINE_LOG_WARN(eng::Channels::kEditor, "could not copy '{}': {}",
                            virtualPath, error);
        }
    }

    // ---- 6. say what happened --------------------------------------------
    //
    // The number that matters is what was LEFT BEHIND. If that is zero on a
    // project with an asset pack in it, this step is not doing its job.
    int            presentFiles = 0;
    std::uintmax_t presentBytes = 0;
    MeasureAssets(assetsDir, presentFiles, presentBytes);
    const int leftBehind = presentFiles - assetsCopied;

    ENGINE_LOG_INFO(eng::Channels::kEditor,
                    "exported '{}' to '{}': {} file(s) total", gameName, dest.string(),
                    filesCopied);
    ENGINE_LOG_INFO(eng::Channels::kEditor,
                    "assets: packed {} of the {} file(s) under assets/ - {} unused "
                    "file(s) were NOT shipped ({} scene(s) scanned{})",
                    assetsCopied, presentFiles, leftBehind < 0 ? 0 : leftBehind,
                    sceneCount,
                    manifestCount > 0
                        ? ", " + std::to_string(manifestCount) + " from export.json"
                        : std::string{});

    if (leftBehind > 0) {
        ENGINE_LOG_INFO(eng::Channels::kEditor,
                        "if something is missing from the exported game, it is an asset "
                        "a script loads by a name built while running - no scene refers "
                        "to it, so nothing could find it. List it in assets/export.json.");
    }

#if defined(ENGINE_SCRIPT_IS_DEBUG) && ENGINE_SCRIPT_IS_DEBUG
    // Worth saying loudly, because the symptom otherwise appears on somebody
    // else's computer: a dialog about a missing ucrtbased.dll, from a game
    // that runs perfectly here.
    ENGINE_LOG_WARN(eng::Channels::kEditor,
                    "this is a DEBUG build. It needs the debug C++ runtime, which "
                    "cannot be redistributed, so this export only runs on a machine "
                    "with Visual Studio installed. Build the Release preset and export "
                    "again for a game you intend to hand out.");
#else
    ENGINE_LOG_INFO(eng::Channels::kEditor,
                    "the target machine needs the Visual C++ Redistributable, which is "
                    "what supplies MSVCP140.dll and friends");
#endif

    result.ok = true;
    result.summary =
        "exported " + gameName + " (" + std::to_string(filesCopied) + " files)";
    return result;
}

} // namespace editor
