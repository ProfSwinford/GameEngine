// ============================================================================
//  EditorApp.cpp - the editor's shell. See EditorApp.h.
//
//  The interesting part of this file is Run(), which decides the order things
//  happen in during one frame of the editor. That order is not obvious and
//  every step of it is there for a reason.
// ============================================================================

#include "EditorApp.h"

#include <engine/core/LogBuffer.h>
#include <engine/debug/Debug.h>
#include <engine/resource/ImportSettings.h>

#include <algorithm>

#include "EditorGui.h"
#include "ObjectMenu.h"
#include "EditorSession.h"
#include "GameExport.h"
#include "ScriptBuild.h"
#include "ScriptIde.h"
#include "ProjectMenu.h"
#include "Relaunch.h"
#include "Shortcuts.h"
#include "UndoStack.h"

#include "panels/AssetBrowserPanel.h"
#include "panels/BuildPanel.h"
#include "panels/ConsolePanel.h"
#include "panels/GamePanel.h"
#include "panels/HierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/ScenePanel.h"
#include "panels/TilePalettePanel.h"

#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <map>

namespace editor {

EditorState& EditorState::Get() {
    static EditorState state;
    return state;
}

// ---------------------------------------------------------------------------
//  The selection. See the note at the top of EditorApp.h.
// ---------------------------------------------------------------------------

bool EditorState::IsSelected(eng::EntityId id) const {
    return std::find(selection.begin(), selection.end(), id) != selection.end();
}

void EditorState::SelectOnly(eng::EntityId id) {
    selectedAsset.clear();
    selection.clear();
    if (!id.IsNull()) {
        selection.push_back(id);
    }
    selected = id;
}

void EditorState::AddToSelection(eng::EntityId id) {
    if (id.IsNull()) {
        return;
    }
    selectedAsset.clear();
    if (!IsSelected(id)) {
        selection.push_back(id);
    }
    // Active even when it was already in the list, because a Shift-click
    // onto something already selected still means "this is where I am now",
    // and the next range should measure from here.
    selected = id;
}

void EditorState::ToggleSelection(eng::EntityId id) {
    if (id.IsNull()) {
        return;
    }
    selectedAsset.clear();
    const auto it = std::find(selection.begin(), selection.end(), id);
    if (it == selection.end()) {
        selection.push_back(id);
        selected = id;
        return;
    }

    selection.erase(it);

    // Ctrl-clicking the ACTIVE entity removes it, so something else has to
    // become active - otherwise the Inspector would keep showing an entity
    // that is no longer selected.
    if (selected == id) {
        selected = selection.empty() ? eng::EntityId{} : selection.back();
    }
}

void EditorState::ClearSelection() {
    selectedAsset.clear();
    selection.clear();
    selected = eng::EntityId{};
}

void EditorState::PruneSelection() {
    eng::Scene& scene = eng::Engine::Get().GetScene();

    std::erase_if(selection, [&scene](eng::EntityId id) {
        return scene.Get(id) == nullptr;
    });

    // The active entity has to be one of the survivors. Falling back to the
    // last of them rather than to nothing keeps a Shift-range working after
    // one of its members is deleted, which is the common case.
    if (scene.Get(selected) == nullptr) {
        selected = selection.empty() ? eng::EntityId{} : selection.back();
    }

    // ---- and the selected FILE, for the same reason -----------------------
    //
    // A path goes stale exactly as an entity id does, and in one more way: the
    // file can be deleted from outside the editor entirely. This is the only
    // place that runs every frame REGARDLESS OF WHICH PANEL HAS FOCUS, which
    // is what makes it the right home - the Assets panel used to drop its own
    // stale selection, and could only do so on the frames it was focused.
    //
    // ONE fs::exists PER FRAME, and only while a file is selected. The
    // alternative is an Inspector describing the import settings of something
    // that is not there, with a Revert button aimed at it.
    // ======================================================================
    //  EITHER THE SHEET OR THE PATH ITSELF, and it has to be both tests.
    //
    //  A sliced frame - "textures/hero.png#walk_1" - is not a file, so asking
    //  Exists about it is always false and a frame could never stay selected
    //  for even one frame: clicking one would set it here and have it cleared
    //  before any panel drew. Hence the SheetPathOf test.
    //
    //  But that test alone is wrong the other way round. "textures/hero.png#f0"
    //  can be a REAL FILE - '#' is a legal filename character, and the Assets
    //  panel's own rename dialog will write one - and if no hero.png sits
    //  beside it, SheetPathOf names a file that does not exist. The selection
    //  was then cleared on the very next frame: the Assets panel kept its blue
    //  border on a file that is plainly there while the Inspector showed
    //  "select an entity in the Hierarchy", for ever, with nothing to explain
    //  it.
    //
    //  So it is dropped only when NEITHER reading finds anything. This is the
    //  same ambiguity ResourceManager::LoadTexture resolves the same way: try
    //  the frame, and fall back to the literal path when a real file is there.
    //
    //  ONE OR TWO fs::exists PER FRAME, and only while something is selected.
    //  The second is reached only for a path containing a '#', which is rare
    //  and is the case that was broken.
    // ======================================================================
    if (!selectedAsset.empty() &&
        !eng::FileSystem::Exists(eng::SheetPathOf(selectedAsset)) &&
        !eng::FileSystem::Exists(selectedAsset)) {
        selectedAsset.clear();
    }
}

bool EditorApp::Init(const std::string& projectRoot) {
    // The engine first, then the panels. The interface needs a window, so the
    // window has to exist before it - which is why the editor's interface is a
    // subsystem inside the engine's ordered start-up rather than something the
    // editor starts on its own.
    eng::Engine::Options options;

    // WHICH PROJECT. Normally a real folder, from --project or from a folder
    // argument, and a folder that is not a project makes Init fail rather
    // than silently opening a different one - see eng::FileSystem::UseRoot.
    //
    // EMPTY IS NOW THE UNUSUAL CASE and means "the environment named it":
    // ENGINE_ASSET_ROOT is set and does hold a project, so FileSystem::Init
    // will pick it up by itself. main() does not reach here at all when
    // nobody named a project - it opens the launcher instead of searching
    // upward for one.
    options.projectRoot = projectRoot;

    // The engine does not know what ImGui is. It is handed two functions and
    // calls them at the right moment in its ordered start-up - after the
    // window and renderer exist, and before they are destroyed.
    options.guiInit     = [] { return EditorGui::Init(eng::Engine::Get().GetWindow()); };
    options.guiShutdown = [] { EditorGui::Shutdown(); };

    if (!eng::Engine::Get().Init(options)) {
        return false;
    }

    // The editor opens in EDIT MODE, like Unity: the clock starts paused, so
    // nothing simulates until Play is pressed. Without this the scene would be
    // running the moment the window appeared and every edit would be fighting
    // whatever system owns that value.
    eng::Engine::Get().Clock().SetPaused(true);

    // Find a C++ compiler, and bring the project's scripts up to date if they
    // have been edited since they were last built - which is the usual case
    // after somebody has been working on them outside the editor.
    //
    // This happens AFTER the engine has started so that the log, the file
    // system and the script loader all exist. The engine's start-up will
    // already have loaded whatever library was there; this replaces it when it
    // is out of date, using exactly the same reload path a later edit uses.
    ScriptBuild::Init();
    RebuildScriptsIfChanged();

    // Adding a panel is one line. The two VIEWS are also kept by pointer,
    // because EditorApp has to call RenderView on them after every panel has
    // drawn - see Run().
    auto scene = std::make_unique<ScenePanel>();
    auto game  = std::make_unique<GamePanel>();
    m_scenePanel = scene.get();
    m_gamePanel  = game.get();

    AddPanel(std::move(scene));                        // Scene   - edit the world
    AddPanel(std::move(game));                         // Game    - play the world
    AddPanel(std::make_unique<ToolbarPanel>());        // Play / Pause / Step
    AddPanel(std::make_unique<HierarchyPanel>());      // every entity
    AddPanel(std::make_unique<InspectorPanel>());      // the selected entity
    auto assets         = std::make_unique<AssetBrowserPanel>();
    m_assetBrowserPanel = assets.get();
    AddPanel(std::move(assets));                       // files on disk
    auto tiles         = std::make_unique<TilePalettePanel>();
    m_tilePalettePanel = tiles.get();
    AddPanel(std::move(tiles));                        // tiles, and the brush
    AddPanel(std::make_unique<BuildPanel>());          // what ships, and in what order
    AddPanel(std::make_unique<ConsolePanel>());        // the log

    // LAST, because it may open a different scene and move the Scene view's
    // camera - so it needs the engine running and every panel already built.
    EditorSession::Restore(m_scenePanel);

    // AFTER Restore, which may have opened a different scene than the one the
    // engine started with. The baseline has to be the scene that is actually
    // on screen, or the first Ctrl+Z would step back to a scene nobody opened.
    Undo::Reset();

    ENGINE_LOG_INFO(eng::Channels::kEditor, "editor ready with {} panels",
                    m_panels.size());
    m_ready = true;
    return true;
}

void EditorApp::AddPanel(std::unique_ptr<Panel> panel) {
    m_panels.push_back(std::move(panel));
}

void EditorApp::RefreshSceneList() {
    if (!eng::FileSystem::ListFiles("scenes", eng::kSceneExtension, m_sceneList)) {
        ENGINE_LOG_WARN(eng::Channels::kEditor,
                        "could not read assets/scenes/, so the Open Scene menu will be "
                        "empty");
    }
}

bool IsPrefabChild(eng::EntityId id) {
    eng::Scene& scene = eng::Engine::Get().GetScene();
    return scene.PrefabInstanceOf(id) != nullptr && !scene.IsPrefabRoot(id);
}

const char* PrefabChildRefusal() {
    return "that belongs to a prefab - the prefab would put it straight back. "
           "Unpack the instance first, or change the prefab itself.";
}

// ---- emptying the Console for a new session -------------------------------
//
// See the note beside these two in EditorApp.h, which is where the argument
// for the throttle reset lives.

namespace {

void EmptyTheConsole() {
    // The keys first, then the buffer. Neither call logs anything, so the
    // order is not load-bearing - but clearing last means the very next line
    // written is the first line of the new session.
    eng::Debug::ResetThrottles();
    eng::LogBuffer::Clear();
}

}   // namespace

void ClearConsoleForPlay() {
    if (EditorState::Get().clearConsoleOnPlay) {
        EmptyTheConsole();
    }
}

void ClearConsoleForCompile() {
    if (EditorState::Get().clearConsoleOnCompile) {
        EmptyTheConsole();
    }
}

std::size_t DestroySelection() {
    EditorState& state = EditorState::Get();
    if (state.selection.empty()) {
        return 0;
    }

    // A COPY OF THE LIST FIRST. Queueing a destroy does not remove anything
    // from the selection, but the prune at the top of the next frame will -
    // and iterating the thing that is about to be cleared is how one of these
    // two copies would eventually have grown a bug the other did not.
    const std::vector<eng::EntityId> selected = state.selection;

    // ======================================================================
    //  PREFAB CHILDREN ARE LEFT OUT, and the rest still go.
    //
    //  Refusing the WHOLE delete because one of eight selected entities
    //  happened to be inside a prefab would be the more annoying answer by
    //  far: the obvious next move is to deselect it and try again, which is
    //  exactly what this does for you. What must not happen is the delete
    //  appearing to work on a child and then being undone by the next rebuild
    //  - see IsPrefabChild.
    // ======================================================================
    eng::Scene& scene = eng::Engine::Get().GetScene();

    std::vector<eng::EntityId> doomed;
    std::size_t                refused = 0;
    for (const eng::EntityId id : selected) {
        if (IsPrefabChild(id)) {
            // ==============================================================
            //  UNLESS ITS ROOT IS GOING TOO, in which case it is not being
            //  kept at all - destroying a prefab root destroys the whole
            //  instance, this child included. Without this test, selecting an
            //  instance and one of its children and pressing Delete would do
            //  exactly the right thing and then report "kept 1 prefab
            //  member", which is a message that is simply not true.
            // ==============================================================
            const eng::EntityId root = scene.PrefabRootOf(id);
            const bool rootIsGoingToo =
                std::find(selected.begin(), selected.end(), root) != selected.end();
            if (!rootIsGoingToo) {
                ++refused;
                continue;
            }
            continue;   // taken care of by its root; not queued twice
        }
        doomed.push_back(id);
    }

    for (const eng::EntityId id : doomed) {
        eng::DeferredOps::QueueDestroy(id);
    }
    state.ClearSelection();

    if (refused > 0) {
        ENGINE_LOG_WARN(eng::Channels::kEditor, "kept {} prefab member(s): {}", refused,
                        PrefabChildRefusal());
    }
    if (doomed.empty()) {
        return 0;   // nothing happened, so the scene is not dirty
    }

    state.MarkDirty();
    ENGINE_LOG_INFO(eng::Channels::kEditor, "destroyed {} entit{}", doomed.size(),
                    doomed.size() == 1 ? "y" : "ies");
    return doomed.size();
}

eng::EntityId DuplicateOne(eng::EntityId id) {
    eng::Scene& scene = eng::Engine::Get().GetScene();

    eng::PrefabInstance* instance = scene.PrefabInstanceOf(id);
    if (instance != nullptr && scene.IsPrefabRoot(id)) {
        // So that parentName describes where the original actually is rather
        // than where it was when the instance was built - somebody may have
        // dragged it onto something else since.
        eng::RefreshPrefabPlacement(scene, *instance);

        const std::string path       = instance->prefabPath;
        const std::string parentName = instance->parentName;

        eng::Entity* source = scene.Get(id);
        if (source == nullptr) {
            return {};
        }
        const eng::Vec2  position = source->Transform().LocalPosition();
        const float      rotation = source->Transform().LocalRotation();
        const eng::Vec2  scale    = source->Transform().LocalScale();

        std::string         error;
        const eng::EntityId copy = scene.InstantiatePrefab(path, error);
        if (copy.IsNull()) {
            ENGINE_LOG_WARN(eng::Channels::kEditor, "could not duplicate: {}", error);
            return {};
        }

        if (eng::PrefabInstance* made = scene.PrefabInstanceOf(copy)) {
            made->parentName = parentName;
        }
        if (eng::Entity* made = scene.Get(copy); made != nullptr) {
            // ON TOP OF THE ORIGINAL, including its rotation and scale, which
            // is what makes "duplicate, then drag it aside" one gesture. The
            // parent is set first so the local numbers mean the same thing.
            if (eng::Entity* parent = scene.Get(scene.Find(parentName));
                parent != nullptr) {
                made->Transform().SetParent(&parent->Transform());
            }
            made->Transform().SetLocalPosition(position);
            made->Transform().SetLocalRotation(rotation);
            made->Transform().SetLocalScale(scale);
        }
        return copy;
    }

    std::string         error;
    const eng::EntityId copy = scene.DuplicateEntity(id, error);
    if (copy.IsNull()) {
        ENGINE_LOG_WARN(eng::Channels::kEditor, "could not duplicate: {}", error);
    }
    return copy;
}

void DestroyEntity(eng::EntityId id) {
    if (id.IsNull()) {
        return;
    }
    if (IsPrefabChild(id)) {
        ENGINE_LOG_WARN(eng::Channels::kEditor, "{}", PrefabChildRefusal());
        return;
    }
    eng::DeferredOps::QueueDestroy(id);

    // THE SELECTION IS LEFT TO THE PRUNE at the top of the next frame, which
    // drops any id whose entity has gone - see EditorState::PruneSelection.
    // Clearing it here would be wrong rather than merely unnecessary: this
    // destroys ONE entity, which need not be the selected one, and the
    // Hierarchy's context menu uses it on whatever was right-clicked.
    EditorState::Get().MarkDirty();
}

namespace {

// ---------------------------------------------------------------------------
//  THE ACTIONS THE EDIT MENU AND THE KEYBOARD SHARE.
//
//  File-local rather than methods, because none of them needs anything from
//  EditorApp - and having them here means the menu item and the chord are
//  demonstrably the same code rather than two call sites that look alike.
// ---------------------------------------------------------------------------

// Undo or redo, with the status-bar line it should leave behind.
std::string StepUndoStack(bool forward) {
    std::string error;
    const bool  ok = forward ? Undo::StepForward(error) : Undo::StepBack(error);
    const char* verb = forward ? "redo" : "undo";

    if (!ok) {
        return std::string(verb) + ": " + error;
    }

    // HOW MANY ARE LEFT, because the useful question after pressing Ctrl+Z is
    // "can I keep going?".
    const std::size_t left = forward ? Undo::RedoDepth() : Undo::UndoDepth();
    return std::string(forward ? "redone" : "undone") + " (" + std::to_string(left) +
           " more)";
}

// Copies everything selected and leaves the copies selected.
//
// ONE LEVEL, NOT A SUBTREE. Scene::DuplicateEntity copies the entity and its
// components and gives the copy the same parent - it does not copy children.
// That is its existing behaviour, which the Hierarchy's own Duplicate item
// has always had; this does not quietly change it.
std::size_t DuplicateSelection() {
    // COPIED BEFORE ANY DUPLICATING, because duplicating adds entities and
    // the selection is about to be replaced by the copies.
    const std::vector<eng::EntityId> sources = EditorState::Get().selection;
    if (sources.empty()) {
        return 0;
    }

    std::vector<eng::EntityId> copies;
    for (const eng::EntityId id : sources) {
        // Through the shared door, which is what makes Ctrl+D on a prefab
        // instance produce another instance rather than a loose copy of its
        // root. See DuplicateOne in EditorApp.h.
        const eng::EntityId copy = DuplicateOne(id);
        if (copy.IsNull()) {
            continue;
        }
        copies.push_back(copy);
    }
    if (copies.empty()) {
        return 0;
    }

    // THE COPIES BECOME THE SELECTION, which is what every editor does and the
    // only thing that makes "duplicate, then drag it aside" one gesture - the
    // copy lands exactly on top of the original, so whatever you drag next had
    // better be the copy.
    EditorState::Get().ClearSelection();
    for (const eng::EntityId id : copies) {
        EditorState::Get().AddToSelection(id);
    }
    EditorState::Get().MarkDirty();
    return copies.size();
}

std::size_t SelectEverything() {
    EditorState::Get().ClearSelection();

    std::size_t count = 0;
    eng::Engine::Get().GetScene().ForEach([&count](eng::Entity& entity) {
        EditorState::Get().AddToSelection(entity.Id());
        ++count;
    });
    return count;
}

// Where Save As should start from: the file this scene came from, or a name
// that at least looks like a scene for one that has never been saved.
//
// THREE CALLERS - the menu item, Ctrl+Shift+S, and Ctrl+S on a scene with
// nowhere to save to. It was written out twice before any of them was a
// shortcut, which is two places to forget the extension.
void PrefillSaveAsPath(char* buffer, std::size_t size) {
    const eng::Scene& scene = eng::Engine::Get().GetScene();
    if (!scene.SourcePath().empty()) {
        std::snprintf(buffer, size, "%s", scene.SourcePath().c_str());
    } else {
        std::snprintf(buffer, size, "scenes/untitled%s", eng::kSceneExtension);
    }
}

} // namespace

void EditorApp::SaveScene(const std::string& virtualPath) {
    std::string error;
    if (eng::Engine::Get().SaveScene(virtualPath, error)) {
        EditorState::Get().ClearDirty();

        // THE SAVED STATE IS NOW THIS ONE, so undoing back to it can clear the
        // unsaved marker instead of leaving the title bar claiming work that
        // is on disk. See UndoStack.h.
        Undo::NoteSaved();
        std::snprintf(m_status, sizeof(m_status), "saved %s",
                      eng::Engine::Get().GetScene().SourcePath().c_str());
        m_sceneList.clear();   // the list may have gained a file
    } else {
        std::snprintf(m_status, sizeof(m_status), "save failed: %s", error.c_str());
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "save failed: {}", error);
    }
}

// ---------------------------------------------------------------------------
//  The Edit menu.
//
//  Every item here is reachable by its chord as well, and the hint beside it
//  comes from the same table the chord does - see Shortcuts.h. The one
//  exception is Delete, whose hint is a literal: the KEY is handled by the
//  panels, because which panel has focus decides whether Delete removes
//  entities or a file on disk.
// ---------------------------------------------------------------------------
void EditorApp::DrawEditMenu() {
    using Shortcuts::Action;

    const bool playing = eng::Engine::Get().IsInPlayMode();

    // ---- undo ------------------------------------------------------------
    //
    // Greyed out rather than hidden while playing, with the reason in the
    // tooltip. Undoing into a play session would restore a snapshot that Stop
    // is about to throw away anyway.
    // RECORDED, NOT DONE. This is inside DrawMenuBar, which runs before the
    // panels draw - and undoing replaces every entity in the scene. See
    // EditorApp::UndoRequest.
    ImGui::BeginDisabled(!Undo::CanUndo());
    if (ImGui::MenuItem("Undo", Shortcuts::Hint(Action::Undo))) {
        m_undoRequest = UndoRequest::Back;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (playing) {
            ImGui::SetTooltip("Not while playing - press Stop first.");
        } else if (Undo::UndoDepth() == 0) {
            ImGui::SetTooltip("Nothing to undo yet.");
        } else {
            ImGui::SetTooltip("%zu step(s) available. One step is one edit, or one "
                              "whole drag.", Undo::UndoDepth());
        }
    }

    ImGui::BeginDisabled(!Undo::CanRedo());
    if (ImGui::MenuItem("Redo", Shortcuts::Hint(Action::Redo))) {
        m_undoRequest = UndoRequest::Forward;
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    const bool anySelected = !EditorState::Get().selection.empty();

    ImGui::BeginDisabled(!anySelected);
    if (ImGui::MenuItem("Duplicate", Shortcuts::Hint(Action::Duplicate))) {
        if (const std::size_t made = DuplicateSelection(); made > 0) {
            std::snprintf(m_status, sizeof(m_status), "duplicated %zu entit%s", made,
                          made == 1 ? "y" : "ies");
        }
    }

    // "Del" IS A LITERAL HERE, and it is the one place the one-table rule is
    // knowingly broken. See the note at the top of Shortcuts.h.
    if (ImGui::MenuItem("Delete", "Del")) {
        DestroySelection();
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    if (ImGui::MenuItem("Select All", Shortcuts::Hint(Action::SelectAll))) {
        const std::size_t count = SelectEverything();
        std::snprintf(m_status, sizeof(m_status), "selected %zu entit%s", count,
                      count == 1 ? "y" : "ies");
    }
    ImGui::BeginDisabled(!anySelected);
    if (ImGui::MenuItem("Deselect All", Shortcuts::Hint(Action::DeselectAll))) {
        EditorState::Get().ClearSelection();
    }
    ImGui::EndDisabled();
}

// ---------------------------------------------------------------------------
//  THE KEYBOARD, once a frame.
//
//  Called late in the frame, next to where a requested scene is applied, and
//  for the same reason: undo REPLACES EVERY ENTITY, which cannot happen while
//  the panels are still describing them. See EditorState::requestedScene.
//
//  Every branch here calls the same function the matching menu item does.
//  That is the point of the arrangement rather than a happy accident: a
//  shortcut that is a second implementation of a menu item is a shortcut that
//  will eventually do something slightly different.
// ---------------------------------------------------------------------------
void EditorApp::HandleShortcuts() {
    using Shortcuts::Action;
    eng::Engine& engine = eng::Engine::Get();

    // ---- the scene file --------------------------------------------------
    if (Shortcuts::Pressed(Action::SaveScene)) {
        if (engine.GetScene().SourcePath().empty()) {
            // NOWHERE TO SAVE TO YET, so Ctrl+S means what Save As means.
            // Doing nothing would be defensible and is what this used to do -
            // but a brand new scene is exactly when somebody presses Ctrl+S
            // hardest, and a shortcut that silently does nothing teaches them
            // it is broken.
            PrefillSaveAsPath(m_saveAsPath, sizeof(m_saveAsPath));
            m_openSaveAsPopup = true;
        } else {
            SaveScene({});
        }
    }
    if (Shortcuts::Pressed(Action::SaveSceneAs)) {
        PrefillSaveAsPath(m_saveAsPath, sizeof(m_saveAsPath));
        m_openSaveAsPopup = true;
    }
    if (Shortcuts::Pressed(Action::NewScene)) {
        RequestNewScene();
    }
    if (Shortcuts::Pressed(Action::OpenScene)) {
        RefreshSceneList();
        m_openOpenScenePopup = true;
    }

    // ---- undo ------------------------------------------------------------
    //
    // The chords record a request exactly as the menu items do, and the ONE
    // place that acts on it is at the bottom of this function. Two routes in,
    // one apply point - which is what makes the claim that they are the same
    // code true rather than merely intended.
    if (Shortcuts::Pressed(Action::Undo)) {
        m_undoRequest = UndoRequest::Back;
    }
    if (Shortcuts::Pressed(Action::Redo)) {
        m_undoRequest = UndoRequest::Forward;
    }

    // ---- the selection ---------------------------------------------------
    if (Shortcuts::Pressed(Action::Duplicate)) {
        if (const std::size_t made = DuplicateSelection(); made > 0) {
            std::snprintf(m_status, sizeof(m_status), "duplicated %zu entit%s", made,
                          made == 1 ? "y" : "ies");
        }
    }
    if (Shortcuts::Pressed(Action::SelectAll)) {
        const std::size_t count = SelectEverything();
        std::snprintf(m_status, sizeof(m_status), "selected %zu entit%s", count,
                      count == 1 ? "y" : "ies");
    }
    if (Shortcuts::Pressed(Action::DeselectAll)) {
        EditorState::Get().ClearSelection();
    }

    // ---- the play controls -----------------------------------------------
    //
    // THE SAME THING THE TOOLBAR BUTTONS DO. Ctrl+P in particular has to keep
    // working while playing with the Game view focused, because it is the way
    // back out - the keyboard belongs to the game at that point, but ImGui is
    // still tracking it and a chord with Ctrl in it is not something a game
    // binds.
    if (Shortcuts::Pressed(Action::PlayStop)) {
        if (engine.IsInPlayMode()) {
            engine.ExitPlayMode();
        } else {
            std::string error;
            if (engine.EnterPlayMode(error)) {
                EditorState::Get().focusGameView = true;
            }
        }
    }
    if (Shortcuts::Pressed(Action::PauseResume) && engine.IsInPlayMode()) {
        engine.Clock().SetPaused(!engine.Clock().IsPaused());
    }
    if (Shortcuts::Pressed(Action::Step) && engine.IsInPlayMode() &&
        engine.Clock().IsPaused()) {
        engine.Clock().RequestSingleStep();
    }

    // ---- and the undo step itself, LAST ----------------------------------
    //
    // Whether it came from a chord above or from the Edit menu drawn earlier
    // this frame, it is applied here - the one point in the frame where
    // replacing every entity in the scene is allowed.
    if (m_undoRequest != UndoRequest::None) {
        const bool forward = m_undoRequest == UndoRequest::Forward;
        m_undoRequest      = UndoRequest::None;
        std::snprintf(m_status, sizeof(m_status), "%s", StepUndoStack(forward).c_str());
    }
}

// ---------------------------------------------------------------------------
//  Open Scene..., the dialog Ctrl+O opens.
//
//  Reads the SAME discovered list the File menu's submenu does, so a scene
//  dropped into assets/scenes/ appears in both with no rebuild.
// ---------------------------------------------------------------------------
void EditorApp::DrawOpenScenePopup() {
    if (m_openOpenScenePopup) {
        ImGui::OpenPopup("Open Scene");
        m_openOpenScenePopup = false;
    }

    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal("Open Scene", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    if (m_sceneList.empty()) {
        ImGui::TextDisabled("no scenes found in assets/scenes/");
    }

    const std::string& open = eng::Engine::Get().GetScene().SourcePath();
    for (const std::string& path : m_sceneList) {
        const bool current = (path == open);

        // The one already open is marked and not offered, the same way the
        // submenu does it - reopening the scene you are in is Reload Scene,
        // and it is on the File menu where it says so.
        if (ImGui::Selectable(path.c_str(), current, current ? ImGuiSelectableFlags_Disabled
                                                             : 0)) {
            ImGui::CloseCurrentPopup();
            RequestOpenScene(path);
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
//  "You have unsaved changes" - for New Scene and Open Scene.
//
//  THREE ANSWERS, NOT TWO, for the same reason ProjectMenu's version has
//  three: somebody who deliberately wants to abandon an experiment should not
//  have to save it first. Discard is the honest third option and it is not
//  the default.
//
//  WHY THIS EXISTS AT ALL. Opening a scene used to discard unsaved work
//  silently, which was survivable while it took a deliberate trip through a
//  submenu. Ctrl+N and Ctrl+O are one keystroke each, and undo does not
//  survive a scene change - so without this, the two most standard shortcuts
//  in any editor would be the two fastest ways to lose an hour.
// ---------------------------------------------------------------------------
// What to do once the question has been answered either way.
//
// ONE COPY, because "Save and continue" and "Discard and continue" differ only
// in whether they save first - and the continuing part was written out twice
// before Close Scene became a third thing it could be.
void EditorApp::Continue(Pending what, const std::string& scene) {
    switch (what) {
        case Pending::NewScene:
            StartNewScene();
            break;
        case Pending::CloseScene:
            CloseScene();
            break;
        case Pending::OpenScene:
            // Back round through the field every route uses. The dirty flag
            // is clear or the work was discarded by now, so the gate in Run()
            // will let it through rather than asking again.
            EditorState::Get().requestedScene = scene;
            break;
        case Pending::None:
            break;
    }
}

void EditorApp::DrawDiscardChangesPopup() {
    if (m_openDiscardPopup) {
        ImGui::OpenPopup("Unsaved Scene");
        m_openDiscardPopup = false;
    }

    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal("Unsaved Scene", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const eng::Scene& scene = eng::Engine::Get().GetScene();

    // NAMES THE FILE. "You have unsaved changes" on its own leaves somebody
    // working out which of several things they have been editing it means.
    if (scene.SourcePath().empty()) {
        ImGui::TextUnformatted("This scene has never been saved anywhere.");
    } else {
        ImGui::Text("%s has unsaved changes.", scene.SourcePath().c_str());
    }
    switch (m_pending) {
        case Pending::NewScene:
            ImGui::TextDisabled("Starting a new scene will discard them.");
            break;
        case Pending::CloseScene:
            ImGui::TextDisabled("Closing the scene will discard them.");
            break;
        default:
            ImGui::TextDisabled("Opening another scene will discard them.");
            break;
    }
    ImGui::Separator();

    // A scene with nowhere to be saved to cannot be saved from here; Save As
    // needs a name, and asking for one inside this dialog would be a second
    // question stacked on the first.
    const bool canSave = !scene.SourcePath().empty();

    ImGui::BeginDisabled(!canSave);
    if (ImGui::Button("Save and continue", ImVec2(150, 0))) {
        std::string error;
        if (eng::Engine::Get().SaveScene({}, error)) {
            EditorState::Get().ClearDirty();
            Undo::NoteSaved();
            const Pending     what  = m_pending;
            const std::string where = m_pendingScene;
            m_pending = Pending::None;
            ImGui::CloseCurrentPopup();
            Continue(what, where);
        } else {
            // STAYS OPEN. Carrying on after a failed save would discard
            // exactly the work this dialog exists to protect.
            ENGINE_LOG_ERROR(eng::Channels::kEditor,
                             "not changing scene: the save failed: {}", error);
            std::snprintf(m_status, sizeof(m_status), "save failed: %s", error.c_str());
        }
    }
    ImGui::EndDisabled();
    if (!canSave && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("This scene has no file yet - use File -> Save Scene As... "
                          "first.");
    }

    ImGui::SameLine();
    if (ImGui::Button("Discard and continue", ImVec2(150, 0))) {
        const Pending     what  = m_pending;
        const std::string where = m_pendingScene;
        m_pending = Pending::None;
        ImGui::CloseCurrentPopup();
        Continue(what, where);
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        // Nothing happens at all, including to the request.
        m_pending = Pending::None;
        m_pendingScene.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorApp::DrawSaveAsPopup() {
    if (m_openSaveAsPopup) {
        ImGui::OpenPopup("Save Scene As");
        m_openSaveAsPopup = false;
    }

    // Centred, because a dialog that opens under the mouse in a docked editor
    // is as likely to be half off the screen as not.
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal("Save Scene As", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("File name, relative to assets/");
    ImGui::SetNextItemWidth(420.0f);
    const bool submitted =
        ImGui::InputText("##saveaspath", m_saveAsPath, sizeof(m_saveAsPath),
                         ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::TextDisabled("for example: scenes/my_level%s", eng::kSceneExtension);

    if (submitted || ImGui::Button("Save", ImVec2(110, 0))) {
        // A NAME TYPED WITHOUT THE EXTENSION STILL SAVES A SCENE.
        //
        // Typing "scenes/boss" used to write a file called exactly that, with
        // no extension at all - which then failed every test for "is this a
        // scene?". So the save succeeded, said so in the status bar, and the
        // file never appeared in the Open Scene menu again. Work that has been
        // saved and cannot be found is worse than a save that refuses, and the
        // person it happens to has no reason to suspect the file name.
        //
        // An EMPTY box is left alone rather than turned into ".oscene".
        // SaveScene({}) already means "save where it came from", which is the
        // sane reading of clearing the box and pressing Save.
        std::string path(m_saveAsPath);
        if (!path.empty() && !eng::FileSystem::HasExtension(path, eng::kSceneExtension)) {
            path += eng::kSceneExtension;
        }
        SaveScene(path);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

bool EditorApp::AskedAboutUnsavedWork(Pending what, const std::string& scene) {
    if (!EditorState::Get().IsDirty()) {
        return false;
    }
    m_pending          = what;
    m_pendingScene     = scene;
    m_openDiscardPopup = true;
    return true;
}

void EditorApp::RequestNewScene() {
    if (eng::Engine::Get().IsInPlayMode()) {
        // REFUSED RATHER THAN DONE. Starting a new scene mid-play would be
        // undone by Stop a moment later, which restores the pre-play snapshot
        // over whatever is open - leaving the old scene's entities with the
        // new scene's empty source path. Undo refuses while playing for the
        // same reason and says so the same way.
        std::snprintf(m_status, sizeof(m_status),
                      "stop playing first - Stop puts the scene back");
        return;
    }
    if (AskedAboutUnsavedWork(Pending::NewScene, {})) {
        return;
    }
    StartNewScene();
}

void EditorApp::RequestOpenScene(const std::string& virtualPath) {
    if (virtualPath.empty()) {
        return;
    }

    // ONLY RECORDED. Not checked for unsaved work here and not loaded here -
    // both happen where requestedScene is applied, in Run(), because five
    // other routes set that field directly and the question has to be asked
    // for all six rather than for the one that remembered.
    EditorState::Get().requestedScene = virtualPath;
}

void EditorApp::RequestCloseScene() {
    if (AskedAboutUnsavedWork(Pending::CloseScene, {})) {
        return;
    }
    CloseScene();
}

void EditorApp::CloseScene() {
    EditorState::Get().ClearSelection();
    eng::Engine::Get().GetScene().Unload();

    // The history described the scene that has just been put away.
    Undo::Reset();
}

void EditorApp::StartNewScene() {
    EditorState::Get().ClearSelection();
    eng::Engine::Get().GetScene().StartNewScene();

    // NOT dirty. An empty scene with no file is not unsaved work; it is
    // nothing. The first edit will set this, and that is the right moment.
    EditorState::Get().ClearDirty();

    // THE HISTORY GOES WITH THE OLD SCENE. Stepping back from a new scene into
    // the one before it would be a Ctrl+Z that reopens a file, which is not
    // what undo means anywhere.
    Undo::Reset();

    std::snprintf(m_status, sizeof(m_status),
                  "new scene - add an object, then Save Scene As");
}

void EditorApp::LoadScene(const std::string& virtualPath) {
    // THE SELECTION IS CLEARED FIRST, and it matters. Loading destroys every
    // entity, and a selection surviving into the new scene would name a slot
    // that a DIFFERENT entity now occupies. The id would be recognised as out
    // of date - which is the mechanism working - but showing nothing at all is
    // better than showing a warning about a selection nobody made.
    EditorState::Get().ClearSelection();

    std::string error;
    if (!eng::Engine::Get().LoadScene(virtualPath, error)) {
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not open '{}': {}", virtualPath,
                         error);
    }

    // EITHER WAY. A load that failed has left the scene in whatever state it
    // got to, and a stack of snapshots from before it describes a scene that is
    // no longer there - so there is even less to be gained from keeping the
    // history after a failure than after a success.
    Undo::Reset();
}

void EditorApp::DrawMenuBar() {
    if (!ImGui::BeginMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("File")) {
        eng::Scene& scene = eng::Engine::Get().GetScene();

        // THE PROJECT ITEMS FIRST, above the scene ones, because a project
        // contains scenes rather than the other way round and the menu should
        // read in that order.
        ProjectMenu::Draw();
        ImGui::Separator();

        if (ImGui::MenuItem("New Scene", Shortcuts::Hint(Shortcuts::Action::NewScene))) {
            RequestNewScene();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Empties the scene and forgets its file, so the next save "
                              "asks where to put it. Asks first if there is unsaved "
                              "work.");
        }

        // The list of scenes is DISCOVERED, not written down. Dropping a
        // .oscene into assets/scenes/ puts it in this menu with no rebuild.
        if (ImGui::BeginMenu("Open Scene")) {
            // Read when the menu is opened rather than every frame: it touches
            // the disk, and scanning a folder sixty times a second for a menu
            // almost nobody has open is exactly the cost a tool should not
            // impose on the thing it is helping with.
            if (m_sceneList.empty()) {
                RefreshSceneList();
            }

            if (m_sceneList.empty()) {
                ImGui::TextDisabled("no scenes found in assets/scenes/");
            }
            for (const std::string& path : m_sceneList) {
                const bool current = (path == scene.SourcePath());
                if (ImGui::MenuItem(path.c_str(), nullptr, current)) {
                    // Through the same door as every other route - the unsaved
                    // question is asked where this is applied. It used to call
                    // LoadScene straight from here, which is why the submenu
                    // was the one route that discarded work silently while the
                    // guide said otherwise.
                    RequestOpenScene(path);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Rescan")) {
                RefreshSceneList();
            }
            ImGui::EndMenu();
        } else {
            m_sceneList.clear();   // read it again next time the menu opens
        }

        // ---- the same list again, as a dialog -----------------------------
        //
        // TWO ROUTES TO ONE THING, deliberately, because they are good at
        // different jobs. The submenu above is the quickest way to pick a
        // scene with the mouse and shows the list without a click. This is
        // what Ctrl+O opens: a dialog the keyboard can reach, which is also
        // the only way the chord is discoverable - ImGui::BeginMenu takes no
        // shortcut hint, so a submenu cannot advertise one.
        //
        // The "..." is the usual mark for "this opens something", which is
        // what keeps the two from reading as duplicates.
        if (ImGui::MenuItem("Open Scene...",
                            Shortcuts::Hint(Shortcuts::Action::OpenScene))) {
            RefreshSceneList();
            m_openOpenScenePopup = true;
        }

        const bool hasPath = !scene.SourcePath().empty();

        // Greyed out rather than hidden when there is nothing to reload, so
        // the menu does not change shape underneath somebody.
        ImGui::BeginDisabled(!hasPath);
        if (ImGui::MenuItem("Reload Scene", nullptr, false, hasPath)) {
            // ---- THE ONE SCENE CHANGE THAT DOES NOT ASK FIRST ------------
            //
            // Deliberately. "Reload" means "throw away what I have done and
            // read the file again" - discarding IS the request, so a dialog
            // asking whether to discard would be asking whether somebody
            // meant what they just clicked. The tooltip says so instead.
            //
            // It goes through requestedScene like every other route, so the
            // loading still happens at the one safe point in the frame.
            EditorState::Get().requestedScene = scene.SourcePath();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Reads the file again and DISCARDS unsaved changes "
                              "without asking - that is what reloading means. Undo "
                              "does not survive it either.");
        }

        ImGui::Separator();

        // ---- AN EMPTY SCENE IS SAVEABLE, and it was not ------------------
        //
        // Both of these used to require scene.IsLoaded(), which means "has at
        // least one entity". Two things that ought to work did not: saving a
        // scene you have just emptied on purpose, and saving the first scene
        // of a brand new project at all. Only Save needs a FILE to save to;
        // neither needs contents.
        ImGui::BeginDisabled(!hasPath);
        if (ImGui::MenuItem("Save Scene", Shortcuts::Hint(Shortcuts::Action::SaveScene),
                            false, hasPath)) {
            SaveScene({});
        }
        ImGui::EndDisabled();

        if (ImGui::MenuItem("Save Scene As...",
                            Shortcuts::Hint(Shortcuts::Action::SaveSceneAs))) {
            // Pre-filled with the current name, so "save a variant of this" is
            // an edit rather than retyping the whole path.
            PrefillSaveAsPath(m_saveAsPath, sizeof(m_saveAsPath));
            m_openSaveAsPopup = true;
        }

        ImGui::Separator();

        ImGui::BeginDisabled(!scene.IsLoaded());
        if (ImGui::MenuItem("Close Scene", nullptr, false, scene.IsLoaded())) {
            RequestCloseScene();
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::Separator();

        // BUILD THE GAME. Asks for a folder and copies a standalone game into
        // it - see GameExport.h for what that folder contains and why.
        //
        // The dialog is opened here but the export is NOT run here. The folder
        // picker answers later, possibly on another thread, and exporting
        // rebuilds the scripts - which replaces the library the running
        // scene's script objects live in. Both reasons point the same way: the
        // work happens at the top of a frame, in Run(). It is the same
        // argument requestedScene makes, and the editor does not get an
        // exemption from its own rules.
        if (ImGui::MenuItem("Build Game...")) {
            GameExport::AskForDestination();
            std::snprintf(m_status, sizeof(m_status), "choose a folder to build into");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Builds the scripts and copies a standalone game - the "
                              "player, the engine, your assets and your settings - into "
                              "a folder you choose.");
        }

        ImGui::Separator();

        // ---- writing scripts somewhere other than here --------------------
        //
        // OFFERED, NEVER DONE FOR YOU. Both of these write into the project
        // folder, and ProjectMenu.cpp states the rule they follow: putting a
        // file in somebody's folder because they opened it is how a tool
        // stops being trusted. See ScriptIde.h for what the two files are.
        if (ImGui::MenuItem("Set Up IDE IntelliSense")) {
            std::string summary;
            std::string error;
            if (WriteIdeFiles(summary, error)) {
                std::snprintf(m_status, sizeof(m_status), "%s", summary.c_str());
            } else {
                std::snprintf(m_status, sizeof(m_status), "%s", error.c_str());
                ENGINE_LOG_ERROR(eng::Channels::kEditor,
                                 "could not write the IDE files: {}", error);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Writes CppProperties.json and compile_commands.json into "
                              "the project folder, so an editor outside this one knows "
                              "where the engine's headers are and which standard your "
                              "scripts are compiled at. Without them every eng:: name "
                              "in a script reads as unknown.");
        }

        if (ImGui::MenuItem("Open Project in Visual Studio")) {
            std::string error;
            if (OpenInVisualStudio(error)) {
                std::snprintf(m_status, sizeof(m_status), "opening Visual Studio");
            } else {
                std::snprintf(m_status, sizeof(m_status), "%s", error.c_str());
                ENGINE_LOG_ERROR(eng::Channels::kEditor,
                                 "could not open Visual Studio: {}", error);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Opens the project FOLDER, which is the only thing Visual "
                              "Studio will read CppProperties.json for. Opening a lone "
                              "script - which is what double-clicking one does - is the "
                              "case with no IntelliSense at all.");
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Exit")) {
            eng::Engine::Get().RequestQuit();
        }
        ImGui::EndMenu();
    }

    // ---- Edit --------------------------------------------------------------
    if (ImGui::BeginMenu("Edit")) {
        DrawEditMenu();
        ImGui::EndMenu();
    }

    // ---- Objects -----------------------------------------------------------
    //
    // The same items the Hierarchy's right-click menu offers, drawn from the
    // one list so the two cannot drift apart. See ObjectMenu.h.
    if (ImGui::BeginMenu("Objects")) {
        if (const eng::EntityId created = DrawObjectMenuItems(); !created.IsNull()) {
            EditorState::Get().SelectOnly(created);
            EditorState::Get().MarkDirty();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        // The loop that means this menu never has to be edited again. Every
        // panel ever added appears here for free.
        for (const std::unique_ptr<Panel>& panel : m_panels) {
            ImGui::MenuItem(panel->Title(), nullptr, panel->OpenFlag());
        }
        ImGui::Separator();
        ImGui::MenuItem("ImGui Demo", nullptr, &EditorState::Get().showImGuiDemo);
        ImGui::EndMenu();
    }

    // A permanent frame-rate readout, because the number you most want is the
    // one you would otherwise have to open a panel to see.
    ImGui::Separator();
    ImGui::Text("%.1f FPS", static_cast<double>(ImGui::GetIO().Framerate));

    if (EditorState::Get().IsDirty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f), "| UNSAVED");
    }
    if (m_scriptBuildFailed) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.32f, 1.0f), "| SCRIPTS DID NOT BUILD");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The compiler's messages are in the Console. Your scripts "
                              "are not running until this is fixed.");
        }
    }
    if (m_status[0] != '\0') {
        ImGui::SameLine();
        ImGui::TextDisabled("| %s", m_status);
    }

    ImGui::EndMenuBar();
}

void EditorApp::DrawPanels() {
    // BEFORE ANY PANEL READS IT. An entity destroyed last frame leaves its id
    // behind in the selection, and a panel that found nothing there would
    // have to cope with a hole. One sweep here means none of them do.
    EditorState::Get().PruneSelection();

    // The Begin/End pair lives HERE, not inside each panel's Draw. See the
    // note in Panel.h.
    for (const std::unique_ptr<Panel>& panel : m_panels) {
        // RECORDED FOR EVERY PANEL, EVERY FRAME, including the ones that
        // return early below - a stale "visible" would have the world
        // rendered into a closed panel's picture. See Panel::IsVisible.
        panel->SetVisible(false);

        if (!panel->IsOpen()) {
            panel->OnHidden();
            continue;
        }
        // Begin returns FALSE for a collapsed window or a background tab - the
        // panel is open but not visible this frame. Telling the panel so is
        // what keeps it from acting on stale information; see Panel::OnHidden.
        if (ImGui::Begin(panel->Title(), panel->OpenFlag())) {
            panel->SetVisible(true);
            panel->Draw();
        } else {
            panel->OnHidden();
        }
        ImGui::End();
    }

    if (EditorState::Get().showImGuiDemo) {
        // Left in on purpose. It is the best reference for ImGui there is:
        // every widget on screen, live, with a "show source" button beside
        // each one. Better than any tutorial, most of which are written
        // against much older versions.
        ImGui::ShowDemoWindow(&EditorState::Get().showImGuiDemo);
    }
}

void EditorApp::FocusGameViewIfRequested() {
    if (!EditorState::Get().focusGameView || m_gamePanel == nullptr) {
        return;
    }

    // A panel that is CLOSED has never been shown to ImGui, so there is no
    // window of that name to focus yet. Open it and try again next frame -
    // one frame of delay in a case almost nobody hits, rather than a focus
    // call that silently does nothing.
    if (!m_gamePanel->IsOpen()) {
        m_gamePanel->SetOpen(true);
        return;
    }

    EditorState::Get().focusGameView = false;

    // Focused BY NAME rather than through the panel, because when Play is
    // pressed the Game view is the BACKGROUND TAB - which is precisely the
    // case where its own Draw does not run. See GamePanel.h.
    ImGui::SetWindowFocus(m_gamePanel->Title());
}

void EditorApp::RevealAssetsPanelIfRequested() {
    if (EditorState::Get().revealAsset.empty() || m_assetBrowserPanel == nullptr) {
        return;
    }

    // Opened first and focused on the NEXT frame, exactly as the Game view and
    // the Tile Palette are, and for the same reason: a closed panel has never
    // been shown to ImGui, so there is no window of that name to focus yet.
    if (!m_assetBrowserPanel->IsOpen()) {
        m_assetBrowserPanel->SetOpen(true);
        return;
    }

    // THE REQUEST IS NOT CONSUMED HERE. All this does is make sure the panel
    // will be DRAWN, so that its own RevealRequestedAsset can navigate to the
    // folder and select the file - which only it knows how to do. Clearing the
    // flag here would bring the tab forward and select nothing.
    ImGui::SetWindowFocus(m_assetBrowserPanel->Title());
}

void EditorApp::RevealTilePaletteIfRequested() {
    TilePaintState& tiles = TilePaintState::Get();
    if (!tiles.revealRequested || m_tilePalettePanel == nullptr) {
        return;
    }

    // Opened first and focused on the NEXT frame, exactly as the Game view is
    // and for the same reason: a closed panel has never been shown to ImGui,
    // so there is no window of that name to focus yet.
    if (!m_tilePalettePanel->IsOpen()) {
        m_tilePalettePanel->SetOpen(true);
        return;
    }

    tiles.revealRequested = false;
    ImGui::SetWindowFocus(m_tilePalettePanel->Title());
}

// ---------------------------------------------------------------------------
//  Apply, Revert and Unpack. See PrefabRequest in EditorApp.h for why none of
//  them happens where it was asked for.
// ---------------------------------------------------------------------------
void EditorApp::ApplyPrefabRequest() {
    EditorState& state = EditorState::Get();

    // TAKEN AND CLEARED FIRST, so that an early return below cannot leave a
    // request that tries again on every frame for the rest of the session.
    // Same reason InspectorPanel::ApplyPendingRemoval does it.
    const PrefabRequest request = state.prefabRequest;
    state.prefabRequest         = PrefabRequest{};

    if (request.action == PrefabAction::None) {
        return;
    }

    eng::Engine& engine = eng::Engine::Get();
    eng::Scene&  scene  = engine.GetScene();

    // ======================================================================
    //  NOT WHILE THE GAME IS RUNNING, and Apply is the reason.
    //
    //  During play the scene is a throwaway copy that Stop is about to replace
    //  - see Engine::EnterPlayMode. Applying from it would write a play
    //  session into the asset: an enemy saved at wherever it had walked to,
    //  with whatever health it had left, pushed out to every scene in the
    //  project. Reverting is only wrong rather than destructive, and is
    //  refused with it so that the rule is one rule.
    // ======================================================================
    if (engine.IsInPlayMode()) {
        std::snprintf(m_status, sizeof(m_status),
                      "prefabs cannot be changed while the game is running - press Stop");
        return;
    }

    // ---- making a new one is the one request with no instance yet ---------
    if (request.action == PrefabAction::Create) {
        eng::EntityId made;
        std::string   error;
        if (!eng::CreatePrefabFromEntity(scene, request.member, request.path, made,
                                         error)) {
            std::snprintf(m_status, sizeof(m_status), "%s", error.c_str());
            ENGINE_LOG_WARN(eng::Channels::kEditor, "{}", error);
            return;
        }
        state.SelectOnly(made);
        state.MarkDirty();

        // Shown in the Assets panel, which is also how you find out where it
        // went - the folder is decided for you, so being told is the point.
        state.revealAsset = request.path;
        std::snprintf(m_status, sizeof(m_status), "created %s", request.path.c_str());
        return;
    }

    eng::PrefabInstance* instance = scene.PrefabInstanceOf(request.member);
    if (instance == nullptr) {
        // The entity was destroyed, or unpacked, between the click and here.
        std::snprintf(m_status, sizeof(m_status),
                      "that entity is no longer part of a prefab instance");
        return;
    }

    // ======================================================================
    //  NOTED BEFORE ANYTHING HAPPENS: WHICH MEMBER, NOT WHICH ENTITY.
    //
    //  Every operation below except Unpack rebuilds the instance, so
    //  request.member is a dead id by the time there is a selection to put
    //  back. The instance RECORD survives, and a local id means the same
    //  member of it before and after - which is the whole point of local ids;
    //  see scene/Prefab.h.
    // ======================================================================
    const int         localId = scene.PrefabLocalIdOf(request.member);
    const std::string path    = instance->prefabPath;

    std::string error;
    bool        ok    = false;
    const char* verb  = "";
    bool        kept  = true;   // false once `instance` must not be touched again

    switch (request.action) {
        case PrefabAction::ApplyAll:
            ok   = eng::ApplyPrefabInstance(scene, request.member, error);
            verb = "applied to";
            break;
        case PrefabAction::RevertAll:
            ok   = eng::RevertPrefabInstance(scene, request.member, error);
            verb = "reverted to";
            break;
        case PrefabAction::ApplyItem:
            ok   = eng::ApplyPrefabItem(scene, request.member, request.item, error);
            verb = "applied to";
            break;
        case PrefabAction::RevertItem:
            ok   = eng::RevertPrefabItem(scene, request.member, request.item, error);
            verb = "reverted to";
            break;
        case PrefabAction::Unpack:
            // UNPACK DESTROYS THE RECORD, so `instance` is dangling from here
            // on and nothing below may dereference it.
            ok   = scene.UnpackPrefabInstance(request.member);
            verb = "unpacked from";
            kept = false;
            if (!ok) {
                error = "that entity is not part of a prefab instance";
            }
            break;
        case PrefabAction::Create:
        case PrefabAction::None:
            return;   // Create returned above; None cannot reach here
    }

    if (!ok) {
        std::snprintf(m_status, sizeof(m_status), "%s", error.c_str());
        ENGINE_LOG_WARN(eng::Channels::kEditor, "{}", error);
        return;
    }

    state.MarkDirty();

    // ---- and put the selection back ---------------------------------------
    //
    // Unpack left every entity alone, so its ids are still good. The other
    // four rebuilt them, and without this the selection would simply vanish
    // on the next prune - which looks like the editor forgetting what you
    // were working on. See EditorState::PruneSelection.
    if (kept) {
        const eng::EntityId again = scene.PrefabMember(*instance, localId);
        if (!again.IsNull()) {
            state.SelectOnly(again);
        }
    }

    const std::string detail = request.item.description.empty()
                                   ? std::string{}
                                   : " (" + request.item.description + ")";
    std::snprintf(m_status, sizeof(m_status), "%s %s%s", verb, path.c_str(),
                  detail.c_str());
}
void EditorApp::ReloadChangedPrefabs() {
    // The write time each loaded prefab had when it was last looked at.
    //
    // A function-local static rather than a member, so that <filesystem> stays
    // out of EditorApp.h - it is included by every panel in the editor.
    static std::map<std::string, std::filesystem::file_time_type> seen;

    eng::Scene& scene = eng::Engine::Get().GetScene();

    std::vector<std::string> changed;
    eng::PrefabLibrary::ForEachLoaded([&](const std::string& path) {
        std::error_code ec;
        const auto      when =
            std::filesystem::last_write_time(eng::FileSystem::Resolve(path), ec);
        if (ec) {
            // Deleted or unreadable. Left alone rather than reported: the next
            // thing that asks for it says so with a better message, and a
            // prefab can legitimately be missing for a moment while somebody
            // is moving files about.
            return;
        }

        const auto it = seen.find(path);
        if (it == seen.end()) {
            // First sight of it. NOT treated as a change - everything would
            // rebuild on the first alt-tab after opening a scene.
            seen.emplace(path, when);
            return;
        }
        if (it->second == when) {
            return;
        }
        it->second = when;
        changed.push_back(path);
    });

    for (const std::string& path : changed) {
        // THE CAPTURE COMES FIRST, against the asset as the instances in this
        // scene were built from it. Doing it the other way round would measure
        // each instance against the NEW file and record its current values as
        // deliberate overrides - pinning every instance to what it looked like
        // before the edit, which is the exact opposite of the point. See
        // Scene::RebuildPrefabInstances.
        eng::CapturePrefabOverridesForAsset(scene, path);

        std::string       error;
        const std::size_t rebuilt = scene.RebuildPrefabInstances(path, error);
        if (rebuilt == 0) {
            if (!error.empty()) {
                ENGINE_LOG_WARN(eng::Channels::kEditor, "'{}' changed, but: {}", path,
                                error);
            }
            continue;
        }

        ENGINE_LOG_INFO(eng::Channels::kEditor,
                        "'{}' changed on disk; {} instance(s) in this scene rebuilt",
                        path, rebuilt);
        std::snprintf(m_status, sizeof(m_status), "%s changed - %zu instance(s) updated",
                      path.c_str(), rebuilt);

        // ==================================================================
        //  THE SCENE IS NOT MARKED DIRTY, AND UNDO IS RESET. Both halves are
        //  deliberate and neither is obvious.
        //
        //  NOT DIRTY, because nothing about the SCENE changed - its instance
        //  records are exactly what they were, and saving it now would write
        //  the same file. The asterisk would be a lie.
        //
        //  UNDO RESET, because its baseline is a snapshot of the scene taken
        //  BEFORE the rebuild, and that snapshot holds the prefab's old
        //  contents. Stepping back to it would restore those contents into a
        //  scene whose asset now says something else - and the next diff would
        //  read every one of those stale values as a deliberate override,
        //  quietly detaching the instance from the prefab for good.
        //
        //  Clearing the history is the same thing a scene change does, and an
        //  outside edit to a prefab in the open scene is that kind of event.
        // ==================================================================
        Undo::Reset();
    }
}

void EditorApp::RebuildScriptsIfChanged() {
    // ======================================================================
    //  ASKED BEFORE NeedsRebuild, AND THAT ORDER IS THE WHOLE FIX.
    //
    //  A build deletes the objects it is about to produce, so NeedsRebuild
    //  answers "yes" for the entire time one is running. This function is
    //  called on every focus regain - which includes a click back into the
    //  window - so during a compile every such click reached the ExitPlayMode
    //  below and stopped the play session. BeginAsync's own one-build guard is
    //  too late to help: it sits AFTER the stop, so it correctly refused the
    //  duplicate build while the play session had already gone.
    //
    //  Reported as "playtesting stops by itself"; it is this.
    // ======================================================================
    if (ScriptBuild::IsBuilding()) {
        return;
    }
    if (!ScriptBuild::NeedsRebuild()) {
        return;   // the usual case, and it costs only a few timestamp checks
    }

    // ---- Unity's "Clear on Recompile" ------------------------------------
    //
    // BELOW BOTH GUARDS ABOVE AND ABOVE EVERYTHING BELOW, and the window is
    // exactly that wide.
    //
    // Above them it would empty the Console on every focus regain - which
    // includes a click back into the window - because that is when this
    // function runs and both guards are what make it usually do nothing.
    //
    // Below the lines that follow it would eat "scripts changed, so play mode
    // was stopped before rebuilding", which is the one sentence explaining
    // why a playtest just ended, and then BeginBuild's "no scripts found" and
    // "no C++ compiler", which are the only explanation of why nothing got
    // compiled.
    ClearConsoleForCompile();

    eng::Engine& engine = eng::Engine::Get();

    // Play mode is stopped first, the way Unity does when it recompiles.
    //
    // It is not optional here. Reloading destroys every running script object,
    // so a play session cannot survive it - and stopping restores the scene to
    // how it was authored, which is a far better place to come back to than a
    // half-played one with its behaviours missing.
    if (engine.IsInPlayMode()) {
        ENGINE_LOG_INFO(eng::Channels::kEditor,
                        "scripts changed, so play mode was stopped before rebuilding");
        engine.ExitPlayMode();
    }

    // STARTED, not waited for. The editor keeps drawing while the compiler
    // runs; the answer is collected in Run() once it is ready.
    //
    // BeginAsync refuses when one is already in flight, which is what stops
    // a rebuild being started again on every frame that asks - see
    // ScriptBuild.h.
    if (ScriptBuild::BeginAsync()) {
        std::snprintf(m_status, sizeof(m_status), "building scripts...");
    }

    // Push the log to disk now rather than letting it sit in the buffer.
    //
    // A build is exactly the kind of thing you want a record of afterwards,
    // and the most interesting case - a script that compiled and then brought
    // the editor down - is the one where an unflushed buffer would be lost.
    eng::Log::Flush();
}

void EditorApp::Run(int frameLimit) {
    eng::Engine& engine = eng::Engine::Get();

    int frames = 0;

    while (engine.BeginFrame()) {
        if (frameLimit > 0 && frames++ >= frameLimit) {
            break;
        }

        // ==================================================================
        //  COLLECT A FINISHED BUILD, once a frame.
        //
        //  This is where the library is actually loaded and every script
        //  rebound - on the main thread, between frames, with nothing
        //  half way through walking the script list. The worker only ever
        //  ran the compiler. See ScriptBuild.h.
        // ==================================================================
        if (ScriptBuild::Result built; ScriptBuild::TakeResult(built)) {
            std::snprintf(m_status, sizeof(m_status), "%s", built.summary.c_str());
            m_scriptBuildFailed = !built.ok;

            // Flushed HERE rather than when the build started, because the
            // interesting case - a script that compiled and then brought the
            // editor down - produces its output during the build, not before.
            eng::Log::Flush();
        }

        // The user alt-tabbed back to the editor. That is the moment to check
        // whether any script was edited while it was in the background - see
        // ScriptBuild.h for why this is the natural trigger.
        if (engine.Events().FocusGainedThisFrame()) {
            RebuildScriptsIfChanged();
            ReloadChangedPrefabs();

            // And any .oimport edited while we were away. Third in the same
            // list for the same reason as the other two: these are hand-
            // editable text files, and the editor must not be the only way to
            // change one. See ResourceManager::ReloadImportSettings.
            eng::ResourceManager::ReloadImportSettings();

            // AND ANY IMAGE ITSELF THAT WAS EDITED WHILE WE WERE AWAY, which
            // is the gesture this whole list exists for: alt-tab to a paint
            // program, save, alt-tab back, see it.
            //
            // HERE RATHER THAN IN A PANEL, and that is load-bearing: this runs
            // before EditorGui::BeginFrame, so no interface has been built and
            // no texture handle is in any draw list yet. Replacing a texture's
            // contents anywhere further down the frame would free a handle the
            // renderer still intends to use.
            if (const std::size_t reread =
                    eng::ResourceManager::ReloadChangedTextures();
                reread > 0) {
                // The Assets panel's rows are rebuilt so that a sheet whose
                // slice changed with it shows the right parts.
                EditorState::Get().assetsNeedRefresh = true;
            }

            // A sidecar edited outside can have SLICED a sheet, which changes
            // what the Assets panel should SHOW and not merely how an image is
            // scaled. See EditorState::assetsNeedRefresh.
            EditorState::Get().assetsNeedRefresh = true;
        }

        // The folder picker from File -> Build Game... answers whenever the
        // user gets round to it, so it is COLLECTED here rather than waited
        // for. This spot is deliberate: it is the same point in the frame
        // where a script rebuild is already known to be safe, and exporting
        // rebuilds the scripts.
        // EVERY OUTCOME IS REPORTED, including the two that used to be silent.
        // A Build that does nothing and says nothing is the worst way for this
        // to fail, and the failure is invisible precisely when somebody most
        // needs to know - see GameExport::Choice.
        //
        // These go to the log at ERROR rather than Info or Warning on purpose.
        // config/engine.json sets a threshold, and when it is "Error" - as it
        // is in this project right now - every Info and Warning line is
        // dropped before it reaches the Console panel OR the log file. A
        // problem with something the user just asked for has to survive that.
        // A folder picked for New or Open Project, collected at the same
        // point and for the same reason as the export's: it is the spot in
        // the frame where touching the scene and the engine is already known
        // to be safe.
        ProjectMenu::CollectPickerResult();

        switch (const GameExport::Pending pending = GameExport::TakePending();
                pending.choice) {
            case GameExport::Choice::None:
                break;

            case GameExport::Choice::Cancelled:
                std::snprintf(m_status, sizeof(m_status), "build cancelled");
                break;

            case GameExport::Choice::Failed:
                std::snprintf(m_status, sizeof(m_status), "the folder picker failed");
                ENGINE_LOG_ERROR(eng::Channels::kEditor,
                                 "the folder picker could not run: {}", pending.error);
                break;

            case GameExport::Choice::Chosen: {
                const GameExport::Result exported = GameExport::ExportTo(pending.path);
                std::snprintf(m_status, sizeof(m_status), "%s", exported.summary.c_str());
                if (!exported.ok) {
                    ENGINE_LOG_ERROR(eng::Channels::kEditor, "build failed: {}",
                                     exported.summary);
                } else {
                    ENGINE_LOG_INFO(eng::Channels::kEditor, "build finished: {} -> {}",
                                     exported.summary, pending.path);
                }
                break;
            }
        }

        // ==================================================================
        //  THE ORDER OF ONE EDITOR FRAME, and every step is here for a reason.
        //
        //  1. Start the interface's frame. This has to come after the platform
        //     events have been handed to it, which happened inside BeginFrame.
        //
        //  2. Simulate. The panels are drawn AFTER this, not before. Drawing
        //     them first would mean every panel showed the state of the world
        //     BEFORE this frame's simulation - last frame's world with this
        //     frame's label on it.
        //
        //  3. Draw the panels.
        //
        //  4. THEN render the two views, because each one sizes its picture
        //     from the panel's content area, which ImGui only knows once the
        //     panel has been laid out.
        //
        //  5. Finish the interface's frame, which is when it actually draws -
        //     so the pictures filled in at step 4 are current rather than one
        //     frame behind.
        // ==================================================================
        EditorGui::BeginFrame();

        // ==================================================================
        //  THE ONE FRAME A PLAY SESSION BEGINS - Unity's "Clear on Play".
        //
        //  NOTICED HERE RATHER THAN WHERE PLAY IS PRESSED, for two reasons.
        //
        //  There are two ways in - the Toolbar's button and Ctrl+P - and the
        //  two are already a verbatim copy of one another. A third identical
        //  line in each is the drift this file argues against everywhere
        //  else. And EnterPlayMode is allowed to refuse: with no scene open
        //  it returns false having logged nothing at all, so a clear at the
        //  call site would empty the Console and leave it empty with no
        //  explanation of why. The flag turning true is the only thing that
        //  means a session really started.
        //
        //  AND IT HAS TO SIT ABOVE Simulate, the next line. Play is entered
        //  during DrawPanels or HandleShortcuts, both below this, so the
        //  session's first simulation step is the one on the NEXT frame -
        //  the line below. Clearing above it empties the Console before any
        //  script has run. Clearing below it, beside FocusGameViewIfRequested
        //  where it would otherwise belong, wipes the OnStart and first
        //  OnUpdate output, which is the whole of what somebody who just
        //  pressed Play is waiting to read.
        //
        //  "play mode started" is logged inside EnterPlayMode on the frame
        //  before this one, so it does not survive the clear. Unity's
        //  Console is empty at the start of a session too.
        // ==================================================================
        const bool inPlayMode = engine.IsInPlayMode();
        if (inPlayMode && !m_wasInPlayMode) {
            ClearConsoleForPlay();
        }
        m_wasInPlayMode = inPlayMode;

        engine.Simulate();

        EditorGui::BeginDockspace();
        DrawMenuBar();
        EditorGui::EndDockspace();
        DrawPanels();
        DrawSaveAsPopup();
        DrawOpenScenePopup();
        DrawDiscardChangesPopup();

        // OUTSIDE the menu, because a modal opened inside a BeginMenu block
        // closes the moment the menu does - it would appear for one frame and
        // vanish. See ProjectMenu.h.
        DrawNewProjectPopup();
        DrawUnsavedProjectPopup();

        FocusGameViewIfRequested();
        RevealAssetsPanelIfRequested();
        RevealTilePaletteIfRequested();

        // The title carries the project, the scene and whether there is
        // unsaved work, all three of which change while the editor runs. It
        // only touches the window when the string is actually different.
        ProjectMenu::UpdateWindowTitle();

        // Step 4: the two views draw the world into their own pictures.
        //
        // VISIBLE, NOT MERELY OPEN. In the default layout the Scene and Game
        // views are two tabs of one dock node, so one of them is always
        // behind the other - and a whole world render per frame was going
        // into a picture nobody could see. DrawPanels ran above, so these
        // flags are this frame's; a view that has just come to the front
        // renders on the same frame it appears. See Panel::IsVisible.
        if (m_scenePanel != nullptr && m_scenePanel->IsOpen() &&
            m_scenePanel->IsVisible()) {
            m_scenePanel->RenderView();
        }
        if (m_gamePanel != nullptr && m_gamePanel->IsOpen() &&
            m_gamePanel->IsVisible()) {
            m_gamePanel->RenderView();
        }

        // ==================================================================
        //  THE KEYBOARD, HERE AND NOT EARLIER.
        //
        //  This is where Ctrl+S used to be checked by hand, and it had a bug
        //  the move fixes: the old line tested io.KeyCtrl without checking
        //  that Shift and Alt were UP, so Ctrl+Shift+S already triggered a
        //  plain Save. Adding Save As on that chord would have fired both -
        //  the scene saved to its old path AND the dialog opened.
        //  ImGui::IsKeyChordPressed compares the whole modifier state, so the
        //  two cannot overlap. See Shortcuts.h.
        //
        //  It is also the right place for a different reason: undo replaces
        //  every entity in the scene, which is the same thing opening a scene
        //  does, and therefore has to happen at the same point - after both
        //  views have rendered and no panel is still describing an entity.
        //
        //  Checked here rather than through InputMap because a tool shortcut
        //  is not a game action; binding it through the game's input system
        //  would put an editor concern in the game's settings file.
        // ==================================================================
        HandleShortcuts();

        // Apply, Revert and Unpack, for the same reason and at the same point:
        // they rebuild an instance's entities. See PrefabRequest.
        ApplyPrefabRequest();

        // ==================================================================
        //  A SCENE SOMEBODY ASKED TO OPEN, APPLIED HERE - and the unsaved
        //  question asked here too.
        //
        //  After every panel has finished with the entities it was describing,
        //  which is why the load is deferred at all. See
        //  EditorState::requestedScene.
        //
        //  THE QUESTION BELONGS AT THIS POINT, not at the call sites. Six
        //  things open scenes - the File menu's list, the Ctrl+O dialog, a
        //  double-click or a right-click in the Assets panel, a scene dropped
        //  on the Scene view, and the Build panel's Open button - and every
        //  one of them sets this field. Asking here asks for all six; asking
        //  at the call sites asked for one, which is how the guide came to
        //  promise a dialog that four routes never showed.
        // ==================================================================
        if (!EditorState::Get().requestedScene.empty()) {
            const std::string path = EditorState::Get().requestedScene;
            EditorState::Get().requestedScene.clear();

            if (engine.IsInPlayMode()) {
                // Same refusal as New Scene, for the same reason: Stop would
                // put the old scene back over the one just opened.
                std::snprintf(m_status, sizeof(m_status),
                              "stop playing first - Stop puts the scene back");
            } else if (!AskedAboutUnsavedWork(Pending::OpenScene, path)) {
                LoadScene(path);
            }
        }

        // ---- undo's bookkeeping, LAST -------------------------------------
        //
        // After every edit this frame, after any undo applied just above, and
        // after any scene load - so what it compares against is the state the
        // next frame will actually start from. Nothing happens on the vast
        // majority of frames. See UndoStack.h.
        Undo::Poll();

        // The gizmo queue is aged ONCE, after BOTH views have drawn it. Doing
        // it inside the drawing would leave whichever view went second with
        // nothing to draw.
        eng::Gizmos::EndFrame(engine.Clock().RealDeltaSeconds());

        // Back to the window, and clear it. Everything visible is a panel now,
        // so this is just the space behind them.
        eng::Renderer::SetRenderTarget(nullptr);
        eng::Renderer::Clear(eng::Color{12, 12, 15, 255});

        // The keyboard follows focus, and focus followed Play a moment ago.
        EditorGui::SetGameInputFocus(m_gamePanel != nullptr &&
                                          m_gamePanel->HasFocus() &&
                                          engine.IsInPlayMode());

        // Step 5: the interface draws, on top of the world.
        EditorGui::EndFrame();
        engine.PresentFrame();
    }
}

void EditorApp::Shutdown() {
    // ANY BUILD STILL RUNNING IS WAITED FOR, FIRST.
    //
    // A compiler writing into .build/ while the engine tears itself down
    // leaves a half-written library behind, and the worker would be touching
    // the log after the log had been shut down. Waiting is the one place in
    // this whole feature where blocking is the right answer: the editor is
    // closing, so there is nothing left to keep responsive.
    ScriptBuild::WaitForBuild();

    // THE SESSION IS SAVED FIRST, while there is still something to read.
    //
    // It needs the Scene view's camera, which lives in a panel, and the open
    // scene's path, which lives in the engine - so it has to happen before
    // either of them is taken down. Putting this one line after m_panels.clear()
    // would compile perfectly and quietly save nothing but a null camera.
    //
    // ---- AND ONLY IF THERE WAS EVER A SESSION ---------------------------
    //
    // main() calls Shutdown after a FAILED Init on purpose, so this path has
    // to be safe rather than merely unlikely. Init can now fail in the very
    // first subsystem - that is what `--project <not a project>` does - and
    // EditorSession::Save reaches Engine::GetScene(), which returns *m_scene
    // for a pointer the scene subsystem never got as far as setting. The
    // result was an access violation on the way out of a perfectly correct
    // error message.
    //
    // The same guard the engine keeps for the same reason; see the
    // m_initialised check at the top of Engine::Shutdown.
    if (m_ready) {
        EditorSession::Save(m_scenePanel);
    }

    // Panels before the engine. A panel's destructor may still reach engine
    // state, so it has to go first - the same reverse-order rule the engine
    // applies to its own subsystems.
    m_panels.clear();
    eng::Engine::Get().Shutdown();
}

} // namespace editor
