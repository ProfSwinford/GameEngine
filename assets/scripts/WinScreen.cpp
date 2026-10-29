// =============================================================================
//  WinScreen - the "YOU WIN" scene, and the way back out of it.
//
//  The other half of the pair that shows how a game moves between scenes.
//  GameRules asks for THIS scene when the last pickup is collected; this
//  script asks for THAT one when you press Space. Between them they are a
//  complete loop, and neither knows anything about the other beyond a file
//  name.
//
//  -----------------------------------------------------------------------------
//  THE ONE RULE FOR CHANGING SCENE FROM A SCRIPT
//
//      eng::Engine::Get().RequestScene("scenes/collector.oscene");   // yes
//      eng::Engine::Get().LoadScene("scenes/collector.oscene", err); // NO
//
//  Request writes the name down and the engine acts on it at the end of the
//  frame. Load does it immediately - and "immediately" means while this
//  script's OnFixedUpdate is still running. Loading a scene destroys every
//  entity in the old one, which destroys every component, which destroys the
//  object whose code is on the stack at that moment. The call would return
//  into an object that no longer exists.
//
//  That mistake usually APPEARS to work, because the freed memory still holds
//  the old values for a while. It is the kind of bug that shows up weeks
//  later on somebody else's machine. See engine/include/engine/Engine.h.
//
//  -----------------------------------------------------------------------------
//  WHAT "UNLOADING" IS, since nothing here calls it.
//
//  There is no UnloadScene to pair with the load. Loading a scene unloads the
//  current one first - Scene::Load starts with Unload() - and unloading
//  destroys every entity, which detaches every component, which releases
//  every texture that nothing else is still using. So the old level is taken
//  down for you, and the Console line reading "scene unloaded; N image(s)
//  still loaded" is that happening. A scene you leave costs nothing.
// =============================================================================

#include <engine/Engine.h>

namespace {

class WinScreen final : public eng::ScriptBehaviour {
public:
    void OnStart() {
        // The MENU context, on top of the gameplay one the engine pushes at
        // start-up. Contexts stack, so "Confirm" below is answered by the
        // menu's binding while everything else carries on working. Popped in
        // OnDestroy so this scene leaves input exactly as it found it.
        eng::InputMap::PushContext("menu");

        ENGINE_LOG_INFO(eng::Channels::kGame,
                        "WinScreen: press Space or Enter to play again");
    }

    void OnFixedUpdate() {
        DrawMessage();

        // IsPressed is true only on the frame the key goes down. IsDown would
        // be true on every frame it is held, and the scene change would be
        // requested dozens of times - harmless, because the last request of a
        // frame wins, but it would say so in the Console every time.
        if (!eng::InputMap::IsPressed("Confirm")) {
            return;
        }

        // Asked for, not done. The engine carries it out once nothing is
        // walking the list of scripts this one is in. See the note at the top.
        ENGINE_LOG_INFO(eng::Channels::kGame, "WinScreen: back to {}", kGameScene);
        eng::Engine::Get().RequestScene(kGameScene);
    }

    void OnDestroy() {
        // Runs when this scene is unloaded, which is exactly when the request
        // above is carried out. Symmetrical with OnStart: a context that is
        // pushed and never popped stays on the stack for the rest of the run
        // and quietly changes what the next scene's keys do.
        eng::InputMap::PopContext();
    }

private:
    void DrawMessage() const {
        // SCREEN space: measured from the top-left corner of the window
        // rather than from a position in the world, so the text sits still
        // wherever the camera happens to be.
        eng::Gizmos::Text(eng::Vec2{16.0f, 16.0f}, "YOU WIN", eng::Color::Green(), 0.0f,
                          eng::GizmoSpace::Screen);
        eng::Gizmos::Text(eng::Vec2{16.0f, 40.0f}, "every pickup collected",
                          eng::Color::White(), 0.0f, eng::GizmoSpace::Screen);
        eng::Gizmos::Text(eng::Vec2{16.0f, 64.0f}, "press SPACE to play again",
                          eng::Color::White(), 0.0f, eng::GizmoSpace::Screen);
    }

    // The scene to go back to. A file name in one named place, so changing it
    // is one edit - and so that the two scripts that make up this loop do not
    // each carry their own spelling of it.
    //
    // The alternative is RequestSceneByIndex, which takes a NUMBER from the
    // build list in the Build panel. Use that when levels are a sequence and
    // "the next one" is the question; use a name, as here, when a particular
    // scene is meant.
    static constexpr const char* kGameScene = "scenes/collector.oscene";
};

} // namespace

ENGINE_REGISTER_SCRIPT(WinScreen)
