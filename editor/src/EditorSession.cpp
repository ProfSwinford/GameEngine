// ============================================================================
//  EditorSession.cpp - saving and restoring where you were. See the header.
// ============================================================================

#include "EditorSession.h"

#include "EditorApp.h"
#include "panels/GamePanel.h"
#include "panels/ScenePanel.h"

#include <engine/Engine.h>
#include <engine/core/Json.h>
#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>

#include <string>

namespace editor {
namespace {

// Reads a [x, y] pair, leaving the output alone when the value is not one.
// Anything in this file may have been hand-edited or written by an older
// version, so nothing in it is trusted to be the shape it should be.
void ReadVec2(const eng::Json& node, const char* key, eng::Vec2& out) {
    const auto found = node.find(key);
    if (found == node.end() || !found->is_array() || found->size() != 2) {
        return;
    }
    const eng::Json& x = (*found)[0];
    const eng::Json& y = (*found)[1];
    if (x.is_number() && y.is_number()) {
        out = eng::Vec2{x.get<float>(), y.get<float>()};
    }
}

} // namespace

void EditorSession::Restore(ScenePanel* scenePanel, GamePanel* gamePanel) {
    if (!eng::FileSystem::Exists(kVirtualPath)) {
        return;   // first run on this machine
    }

    std::string text;
    std::string error;
    if (!eng::FileSystem::ReadTextFile(kVirtualPath, text, error)) {
        ENGINE_LOG_WARN(eng::Channels::kEditor, "could not read the last session: {}",
                        error);
        return;
    }

    const eng::Json document = eng::ParseJson(text, error);
    if (!error.empty()) {
        // Not worth stopping over, and not worth a red line either: the
        // remedy is automatic, because closing the editor will overwrite it
        // with something valid.
        ENGINE_LOG_WARN(eng::Channels::kEditor,
                        "'{}' is not valid JSON ({}), so the editor opened fresh",
                        kVirtualPath, error);
        return;
    }

    // ---- the scene --------------------------------------------------------
    //
    // ONLY WHEN IT IS NOT ALREADY OPEN. Engine::Init has already loaded the
    // build list's first scene by the time this runs, and in the common case -
    // you were working on that scene - reloading it would throw away a perfectly
    // good scene and build the same one again.
    if (const auto scene = document.find("scene");
        scene != document.end() && scene->is_string()) {
        const std::string wanted = scene->get<std::string>();
        const std::string open   = eng::Engine::Get().GetScene().SourcePath();

        if (!wanted.empty() && wanted != open) {
            if (!eng::FileSystem::Exists(wanted)) {
                // Said plainly, because "the scene I was working on is gone"
                // is something the person needs to know rather than silently
                // being put somewhere else.
                ENGINE_LOG_WARN(eng::Channels::kEditor,
                                "the scene from last session, '{}', no longer exists; "
                                "opening '{}' instead",
                                wanted, open.empty() ? "nothing" : open);
            } else {
                std::string loadError;
                if (eng::Engine::Get().LoadScene(wanted, loadError)) {
                    ENGINE_LOG_INFO(eng::Channels::kEditor,
                                    "resumed last session's scene '{}'", wanted);
                } else {
                    ENGINE_LOG_WARN(eng::Channels::kEditor,
                                    "could not reopen '{}': {}", wanted, loadError);
                }
            }
        }
    }

    // ---- the selection ----------------------------------------------------
    //
    // By NAME, looked up in whatever scene is now loaded. See the header for
    // why an EntityId would be the wrong thing to have written down.
    if (const auto selected = document.find("selected");
        selected != document.end() && selected->is_string()) {
        const std::string name = selected->get<std::string>();
        if (!name.empty()) {
            const eng::EntityId found = eng::Engine::Get().GetScene().Find(name);
            if (!found.IsNull()) {
                EditorState::Get().SelectOnly(found);
            }
        }
    }

    // ---- where the Scene view was looking ---------------------------------
    if (scenePanel != nullptr) {
        const auto view = document.find("sceneView");
        if (view != document.end() && view->is_object()) {
            eng::Vec2 position = scenePanel->Camera().Position();
            ReadVec2(*view, "position", position);
            scenePanel->Camera().SetPosition(position);

            if (const auto zoom = view->find("zoom");
                zoom != view->end() && zoom->is_number()) {
                const float value = zoom->get<float>();
                // Guarded because a zoom of zero or a negative one would make
                // the view undrawable, and SetZoom is not obliged to defend
                // itself against a hand-edited file.
                if (value > 0.0f) {
                    scenePanel->Camera().SetZoom(value);
                }
            }
        }
    }

    // ---- what shape the Game view was rendering at ------------------------
    if (gamePanel != nullptr) {
        const auto view = document.find("gameView");
        if (view != document.end() && view->is_object()) {
            // ==============================================================
            //  THE CUSTOM ENTRIES COME BACK FIRST, because the remembered
            //  choice may BE one of them - and an index into a list that has
            //  not been rebuilt yet would select the wrong row, or none.
            //
            //  Each is re-parsed from its text rather than restored from a
            //  kind and two numbers, so there is one piece of code deciding
            //  what "16:9" means and the file holds what somebody typed.
            // ==============================================================
            if (const auto custom = view->find("custom");
                custom != view->end() && custom->is_array()) {
                for (const eng::Json& entry : *custom) {
                    if (!entry.is_string()) {
                        continue;
                    }
                    GameViewMode made;
                    std::string  why;
                    if (ParseGameViewMode(entry.get<std::string>(), made, why)) {
                        gamePanel->AddCustomMode(made);
                    }
                }
            }

            // ==============================================================
            //  THE CHOSEN SHAPE IS NOT READ FROM HERE ANY MORE, and an older
            //  session's "mode" key is deliberately ignored.
            //
            //  It used to be remembered here, which is exactly what made the
            //  whole feature useless: this file is one person's view of one
            //  project, so pinning 16:9 and exporting produced a game that
            //  stretched to fill whatever window it opened in. The setting
            //  had never left the editor.
            //
            //  It lives in config/engine.json now - the file the engine reads
            //  at start-up and the export copies into the game - and the Game
            //  view selects whatever that says. See editor/src/ProjectSettings.h
            //  for the project-versus-person split, and GamePanel's
            //  SyncChosenFromProject for the reading.
            //
            //  IGNORED RATHER THAN MIGRATED, because the project setting is
            //  now the only answer and a session that disagreed with it would
            //  quietly override what the game is going to do.
            // ==============================================================

            if (const auto scale = view->find("scale");
                scale != view->end() && scale->is_number()) {
                const float value = scale->get<float>();
                // Negative would draw nothing; absurdly large would put the
                // picture so far off the panel there is no way back to it
                // except by editing this file. Zero IS meaningful - it is
                // "fit" - so the floor is zero rather than something above it.
                if (value >= 0.0f && value <= 8.0f) {
                    gamePanel->SetScale(value);
                }
            }
        }
    }
}

void EditorSession::Save(ScenePanel* scenePanel, GamePanel* gamePanel) {
    eng::Json document = eng::Json::object();
    document["_comment"] =
        "Where the editor was when it last closed, so it can open there again. "
        "Written automatically; delete it to start fresh. Panel layout is not here - "
        "ImGui keeps that in imgui.ini. See editor/src/EditorSession.h.";

    document["scene"] = eng::Engine::Get().GetScene().SourcePath();

    // The selected entity's NAME, or "" when nothing is selected. Looked up
    // now rather than remembered, because the id may have gone stale while the
    // editor was running.
    std::string selectedName;
    if (const eng::Entity* entity =
            eng::Engine::Get().GetScene().Get(EditorState::Get().selected)) {
        selectedName = entity->Name();
    }
    document["selected"] = selectedName;

    if (scenePanel != nullptr) {
        const eng::Vec2 position = scenePanel->Camera().Position();
        eng::Json       view     = eng::Json::object();
        view["position"]         = {position.x, position.y};
        view["zoom"]             = scenePanel->Camera().Zoom();
        document["sceneView"]    = view;
    }

    if (gamePanel != nullptr) {
        eng::Json view = eng::Json::object();

        // NOT THE CHOSEN SHAPE. That is a property of the game and lives in
        // config/engine.json - see the note in Restore. Only the scale, which
        // is how large the preview is drawn on THIS monitor.
        view["scale"] = gamePanel->Scale();

        const auto& modes = gamePanel->Modes();

        // ONLY THE CUSTOM ONES. Writing the built-ins down would freeze this
        // version's list into every project, so a preset added later would
        // not appear in a project that had been opened once.
        eng::Json custom = eng::Json::array();
        for (std::size_t i = gamePanel->BuiltInCount(); i < modes.size(); ++i) {
            custom.push_back(modes[i].label);
        }
        if (!custom.empty()) {
            view["custom"] = std::move(custom);
        }

        document["gameView"] = std::move(view);
    }

    std::string error;
    if (!eng::FileSystem::WriteTextFile(kVirtualPath, document.dump(2), error)) {
        // A warning, not an error. Failing to remember where you were is a
        // small loss, and the editor is on its way out - there is nothing left
        // for it to do about it.
        ENGINE_LOG_WARN(eng::Channels::kEditor, "could not save the session: {}", error);
        return;
    }

    ENGINE_LOG_INFO(eng::Channels::kEditor, "session saved: scene '{}'",
                    document["scene"].get<std::string>());
}

} // namespace editor
