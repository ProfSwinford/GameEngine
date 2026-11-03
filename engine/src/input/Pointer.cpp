// ============================================================================
//  Pointer.cpp - see Pointer.h for why this reads the UI's pointer rather
//  than the window's.
// ============================================================================

#include <engine/Engine.h>
#include <engine/input/Pointer.h>
#include <engine/render/Camera.h>
#include <engine/scene/UI.h>

namespace eng {

bool Pointer::IsOverView() { return UISystem::PointerIsActive(); }
Vec2 Pointer::InView()     { return UISystem::PointerPosition(); }
Vec2 Pointer::ViewSize()   { return UISystem::PointerViewport(); }
bool Pointer::IsDown()     { return UISystem::PointerIsDown(); }
bool Pointer::WentDown()   { return UISystem::PointerWentDown(); }
bool Pointer::WentUp()     { return UISystem::PointerWentUp(); }

Vec2 Pointer::InWorld() {
    const Vec2 inView = UISystem::PointerPosition();

    // ======================================================================
    //  NO ENGINE MEANS NO CAMERA, AND THE VIEW POSITION IS RETURNED AS-IS.
    //
    //  Engine::Get() hands back a reference to a static, so it is never null -
    //  but in a test nothing has started it and its camera is default
    //  constructed. Rather than pretend, this asks whether the engine is
    //  actually running and falls back to the identity, which is the one
    //  answer that is obviously not a world position rather than a plausible
    //  wrong one.
    // ======================================================================
    if (!Engine::Get().IsInitialised()) {
        return inView;
    }

    // The camera's own viewport is what ScreenToWorld measures against, and
    // it is set from the render target rather than from the view that drives
    // the pointer. In the editor those are the same size - the Game panel
    // renders at its own size - so this is correct; stated here because the
    // two sizes being equal is a fact about the editor rather than an
    // invariant of the engine.
    return Engine::Get().GetCamera().ScreenToWorld(inView);
}

} // namespace eng
