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

    // ======================================================================
    //  THE VIEWPORT THE POINTER CAME FROM, NOT THE ONE THE CAMERA LAST DREW
    //  INTO. The two are the same in a shipped game and are NOT the same in
    //  the editor, which is the whole reason this is spelled out.
    //
    //  ScreenToWorld measures from the middle of the camera's viewport, and
    //  that viewport is written twice per frame: Engine::BeginFrame sets it
    //  from Renderer::OutputSize() with no target bound, which is the WINDOW,
    //  and RenderWorld later sets it again while the Game panel's picture is
    //  bound, which is the PANEL. Scripts run between those two - Simulate
    //  comes before any panel renders - so a script converting a pointer
    //  position measured inside the panel would have the window's half-extent
    //  applied to it. Wrong by half the difference, every frame, in the
    //  editor only: exactly the kind of split that reads as broken camera
    //  maths rather than as a stale viewport.
    //
    //  A COPY OF THE CAMERA rather than setting it back afterwards, because
    //  this is a query and a query that leaves the camera changed would be a
    //  worse bug than the one it fixes. In a shipped game PointerViewport()
    //  IS Renderer::OutputSize(), so this is a struct copy and an identical
    //  answer.
    // ======================================================================
    Camera view = Engine::Get().GetCamera();
    view.SetViewportSize(UISystem::PointerViewport());
    return view.ScreenToWorld(inView);
}

} // namespace eng
