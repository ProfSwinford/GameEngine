// ============================================================================
//  PhysicsLab - the physics playground's control desk.
//
//  ONE OF THESE, on the scene's root entity. It does three things and nothing
//  else:
//
//    1. Prints what the collision system is actually doing, on screen, every
//       frame. Collider count, touching pairs, how much work the broad phase
//       saved, how many queries ran.
//    2. Resets the whole playground on Interact (E), by asking for its own
//       scene again. Everything has fallen over by then and putting it back
//       by hand is not a test of anything.
//    3. Re-launches every PhysicsLauncher in the scene on Jump (Space).
//
//  ==========================================================================
//  WHY THE READOUT IS HERE AND NOT IN THE EDITOR
//
//  The Toolbar shows the pair count already. The numbers that tell you whether
//  the broad phase is WORKING are the two it does not show: how many pairs the
//  narrow phase actually looked at, and how many it would have looked at
//  without one. A playground whose whole purpose is "is physics doing what it
//  says" should show both, and a script can show them in a built game too.
//
//  ==========================================================================
//  IT DRAWS WITH GIZMOS, SO IT ONLY APPEARS WHILE PLAYING.
//
//  That is not a limitation to work around - a script does not run while the
//  clock is paused, which is the whole of the time you spend editing, and
//  pretending otherwise is how you end up with an editor that quietly runs
//  your game. The one thing that DOES have to be visible while editing is the
//  collider outlines, and those are drawn by the Scene view itself rather than
//  by anything in here. See CollisionSystem::DrawGizmos.
// ============================================================================

#include <engine/Engine.h>

#include <cstdio>


class PhysicsLab final : public eng::ScriptBehaviour {
public:
    // Where the readout sits, in screen pixels from the top left. The Scene
    // view and the Game view are different sizes, so this is deliberately
    // measured from a corner rather than centred.
    eng::Vec2 readoutAt{16.0f, 16.0f};   // screen pixels, from the top left

    bool showReadout = true;   // untick for a clean picture

    // THE SCENE TO RELOAD, which is normally this one. Left empty it asks the
    // engine what the current scene's file was, so a copy of this playground
    // saved under another name still resets to itself rather than to whatever
    // path was typed in here when it was written.
    std::string resetTo;   // empty means "this scene, whatever it is called"

    void OnUpdate() {
        // ---- the reset key ------------------------------------------------
        //
        // RequestScene and never LoadScene. Loading destroys every entity,
        // including this script, and the call would return into a `this` that
        // no longer exists. See the scripting reference.
        if (eng::InputMap::IsPressed("Interact")) {
            const std::string& path = !resetTo.empty() ? resetTo : CurrentScenePath();
            if (!path.empty()) {
                ENGINE_LOG_INFO(eng::Channels::kGame, "physics lab: reset");
                eng::Engine::Get().RequestScene(path);
                return;
            }
            ENGINE_LOG_WARN(eng::Channels::kGame,
                            "physics lab: nothing to reset to - this scene has never "
                            "been saved, so there is no file to load again");
        }

        if (!showReadout) {
            return;
        }

        // ---- the readout --------------------------------------------------
        //
        // Built with snprintf rather than a stream or std::format, because a
        // project's scripts compile as C++20 against whatever the editor
        // happens to have, and this file is meant to be copied into other
        // projects without finding out which library features are present.
        const std::size_t narrow = eng::CollisionSystem::NarrowPhaseTests();
        const std::size_t brute  = eng::CollisionSystem::BruteForcePairs();

        // HOW MUCH THE BROAD PHASE SAVED, as a percentage, and guarded: an
        // empty scene has nothing to compare and 0/0 is not a number.
        const float saved =
            brute > 0 ? 100.0f * (1.0f - static_cast<float>(narrow) /
                                             static_cast<float>(brute))
                      : 0.0f;

        const eng::Vec2 gravity = eng::Physics2D::Gravity();

        char line[256];
        float y = readoutAt.y;

        const auto print = [&](eng::Color colour) {
            eng::Gizmos::Text(eng::Vec2{readoutAt.x, y}, line, colour);
            y += 16.0f;
        };

        std::snprintf(line, sizeof(line), "PHYSICS LAB   E reset   SPACE launch");
        print(eng::Color::Yellow());

        std::snprintf(line, sizeof(line), "colliders %zu   touching pairs %zu",
                      eng::CollisionSystem::ColliderCount(),
                      eng::CollisionSystem::ActivePairCount());
        print(eng::Color::White());

        std::snprintf(line, sizeof(line),
                      "narrow phase %zu of %zu possible pairs  (%.0f%% skipped)",
                      narrow, brute, static_cast<double>(saved));
        print(eng::Color::White());

        std::snprintf(line, sizeof(line),
                      "solver passes %d   gravity %.0f, %.0f   queries %zu",
                      eng::CollisionSystem::SolverIterations(),
                      static_cast<double>(gravity.x), static_cast<double>(gravity.y),
                      eng::Physics2D::QueriesThisFrame());
        print(eng::Color::Cyan());

        std::snprintf(line, sizeof(line),
                      "collider outlines: green solid, cyan trigger, red touching, "
                      "grey disabled");
        print(eng::Color::Grey());
    }

private:
    // The file this scene came from, asked of the scene rather than remembered.
    // An empty string means it has never been saved anywhere, which is the one
    // case reset cannot do anything about.
    const std::string& CurrentScenePath() const {
        return eng::Engine::Get().GetScene().SourcePath();
    }
};


ENGINE_REGISTER_SCRIPT(PhysicsLab)
