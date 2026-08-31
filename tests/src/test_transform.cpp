// ============================================================================
//  Tests for Transform2D, Mat3 and Camera.
//
//  The parent/child cases are the ones worth having. When a three-level
//  hierarchy comes out wrong on screen, no amount of staring at the window
//  says WHICH of the three levels is at fault - but a failing test here names
//  it immediately.
// ============================================================================

#include <doctest/doctest.h>
#include <engine/render/Camera.h>
#include <engine/math/Transform2D.h>

using namespace eng;

TEST_CASE("an unparented transform's world matrix is its local matrix") {
    Transform2D node;
    node.SetLocalPosition(Vec2{5.0f, -3.0f});
    node.SetLocalRotation(0.7f);
    node.SetLocalScale(Vec2{2.0f, 2.0f});

    CHECK(ApproxEqual(node.WorldMatrix(), node.LocalMatrix()));
    CHECK(ApproxEqual(node.WorldPosition(), Vec2{5.0f, -3.0f}));
}

TEST_CASE("a child inherits its parent's translation") {
    Transform2D parent;
    Transform2D child;
    parent.SetLocalPosition(Vec2{10.0f, 0.0f});
    child.SetLocalPosition(Vec2{5.0f, 0.0f});
    child.SetParent(&parent);

    CHECK(ApproxEqual(child.WorldPosition(), Vec2{15.0f, 0.0f}));
}

TEST_CASE("a child orbits when its parent rotates") {
    // Parent at the origin rotated 90 degrees, child at local (1, 0).
    // The child's world position is (0, 1) - within epsilon, because floating
    // point.
    //
    // GETTING (0, -1) HERE means the rotation direction or the multiplication
    // order disagrees with the convention block at the top of Mat3.h. The fix
    // is to correct the CONVENTION DOCUMENT first and then the code, so the
    // two stay in agreement.
    Transform2D parent;
    Transform2D child;
    parent.SetLocalRotation(kPi * 0.5f);
    child.SetLocalPosition(Vec2{1.0f, 0.0f});
    child.SetParent(&parent);

    CHECK(ApproxEqual(child.WorldPosition(), Vec2{0.0f, 1.0f}));
}

TEST_CASE("a parent's scale scales its child's offset") {
    Transform2D parent;
    Transform2D child;
    parent.SetLocalScale(Vec2{3.0f, 3.0f});
    child.SetLocalPosition(Vec2{2.0f, 0.0f});
    child.SetParent(&parent);

    CHECK(ApproxEqual(child.WorldPosition(), Vec2{6.0f, 0.0f}));
}

TEST_CASE("a three-deep hierarchy composes correctly") {
    // *** THE MILESTONE 1 CHECK. *** Hand-computed:
    //
    //   grandparent: translate (100, 0)
    //   parent:      local (50, 0), rotated +90 deg
    //   child:       local (10, 0)
    //
    //   The parent's world position is (150, 0) - the grandparent has no
    //   rotation, so the offset is unturned.
    //
    //   The child's local (10, 0) passes through the parent's +90 rotation and
    //   becomes (0, 10) in the parent's frame, then translates by the parent's
    //   world position: (150, 10).
    Transform2D grandparent;
    Transform2D parent;
    Transform2D child;

    grandparent.SetLocalPosition(Vec2{100.0f, 0.0f});
    parent.SetLocalPosition(Vec2{50.0f, 0.0f});
    parent.SetLocalRotation(kPi * 0.5f);
    child.SetLocalPosition(Vec2{10.0f, 0.0f});

    parent.SetParent(&grandparent);
    child.SetParent(&parent);

    CHECK(child.Depth() == 2);
    CHECK(ApproxEqual(parent.WorldPosition(), Vec2{150.0f, 0.0f}));
    CHECK(ApproxEqual(child.WorldPosition(), Vec2{150.0f, 10.0f}));

    // Rotating the grandparent must swing the whole arm, not just the parent.
    //
    // Hand-computed again: the child sits at (50, 10) in the grandparent's own
    // frame. A +90 degree rotation sends (x, y) to (-y, x), so that becomes
    // (-10, 50), and the grandparent's translation puts it at (90, 50).
    grandparent.SetLocalRotation(kPi * 0.5f);
    CHECK(ApproxEqual(child.WorldPosition(), Vec2{90.0f, 50.0f}));
}

TEST_CASE("a transform's world position is unaffected by reparenting to an identity parent") {
    // A useful invariant that catches a surprising number of bugs - most of
    // them multiplication-order bugs that identity happens to hide only when
    // the order is right.
    Transform2D identityParent;
    Transform2D node;
    node.SetLocalPosition(Vec2{7.0f, -2.0f});
    node.SetLocalRotation(0.3f);
    node.SetLocalScale(Vec2{1.5f, 1.5f});

    const Vec2 before = node.WorldPosition();
    node.SetParent(&identityParent);
    CHECK(ApproxEqual(node.WorldPosition(), before));
}

TEST_CASE("reparenting with keepWorldTransform does not move the node") {
    Transform2D parent;
    parent.SetLocalPosition(Vec2{40.0f, 10.0f});
    parent.SetLocalRotation(0.9f);
    parent.SetLocalScale(Vec2{2.0f, 2.0f});

    Transform2D node;
    node.SetLocalPosition(Vec2{5.0f, 5.0f});
    const Vec2 before = node.WorldPosition();

    node.SetParent(&parent, /*keepWorldTransform=*/true);
    CHECK(ApproxEqual(node.WorldPosition(), before, 0.01f));
}

TEST_CASE("destroying a parent orphans its children to the root, keeping world position") {
    // The documented answer from Transform2D.h, checked rather than assumed.
    Transform2D child;
    Vec2        worldBefore;
    {
        Transform2D parent;
        parent.SetLocalPosition(Vec2{30.0f, 0.0f});
        child.SetLocalPosition(Vec2{5.0f, 0.0f});
        child.SetParent(&parent);
        worldBefore = child.WorldPosition();
        CHECK(ApproxEqual(worldBefore, Vec2{35.0f, 0.0f}));
    }   // parent destroyed here

    CHECK(child.Parent() == nullptr);
    CHECK(ApproxEqual(child.WorldPosition(), worldBefore));
}

TEST_CASE("local and world point conversions are inverses") {
    Transform2D parent;
    Transform2D node;
    parent.SetLocalPosition(Vec2{12.0f, -4.0f});
    parent.SetLocalRotation(0.6f);
    node.SetLocalPosition(Vec2{3.0f, 3.0f});
    node.SetLocalRotation(-0.2f);
    node.SetLocalScale(Vec2{1.7f, 1.7f});
    node.SetParent(&parent);

    const Vec2 local{2.0f, -5.0f};
    CHECK(ApproxEqual(node.WorldToLocalPoint(node.LocalToWorldPoint(local)), local, 0.01f));
    CHECK(ApproxEqual(node.WorldToLocalVector(node.LocalToWorldVector(local)), local, 0.01f));
}

TEST_CASE("TransformVector ignores translation and TransformPoint does not") {
    // The distinction that makes velocities not drift when an object moves.
    const Mat3 moved = Mat3::Translation(Vec2{100.0f, 100.0f});
    CHECK(ApproxEqual(moved.TransformPoint(Vec2{1.0f, 0.0f}), Vec2{101.0f, 100.0f}));
    CHECK(ApproxEqual(moved.TransformVector(Vec2{1.0f, 0.0f}), Vec2{1.0f, 0.0f}));
}

TEST_CASE("Mat3::Inverse undoes a transform-rotate-scale matrix") {
    const Mat3 m = Mat3::FromTRS(Vec2{13.0f, -7.0f}, 0.85f, Vec2{2.0f, 3.0f});
    const Mat3 identity = m * m.Inverse();
    CHECK(ApproxEqual(identity, Mat3::Identity(), 0.001f));
}

TEST_CASE("screen-to-world round trips at several zoom levels") {
    // This is what makes clicking on things in the Scene view work: a pixel
    // under the mouse has to turn back into a position in the world.
    Camera camera;
    camera.SetViewportSize(Vec2{1280.0f, 720.0f});

    const Vec2 cameraPositions[] = {
        Vec2{0.0f, 0.0f}, Vec2{123.0f, -456.0f}, Vec2{-1000.0f, 900.0f}};
    const float zooms[] = {0.5f, 1.0f, 2.0f, 7.3f};

    for (const Vec2& position : cameraPositions) {
        for (float zoom : zooms) {
            camera.SetPosition(position);
            camera.SetZoom(zoom);

            const Vec2 points[] = {Vec2{0.0f, 0.0f}, Vec2{50.0f, -25.0f},
                                   Vec2{-300.0f, 400.0f}};
            for (const Vec2& point : points) {
                const Vec2 screen = camera.WorldToScreen(point);
                const Vec2 back   = camera.ScreenToWorld(screen);
                CHECK(ApproxEqual(back, point, 0.05f));
            }
        }
    }
}

TEST_CASE("the camera flips y exactly once") {
    // World is y-up, the screen is y-down, and ViewMatrix is the only place
    // the two are reconciled. A world point ABOVE the camera must land ABOVE
    // the centre of the screen, which in screen coordinates means a SMALLER y.
    Camera camera;
    camera.SetViewportSize(Vec2{800.0f, 600.0f});
    camera.SetPosition(Vec2{0.0f, 0.0f});
    camera.SetZoom(1.0f);

    const Vec2 centre = camera.WorldToScreen(Vec2{0.0f, 0.0f});
    const Vec2 above  = camera.WorldToScreen(Vec2{0.0f, 100.0f});
    const Vec2 right  = camera.WorldToScreen(Vec2{100.0f, 0.0f});

    CHECK(ApproxEqual(centre, Vec2{400.0f, 300.0f}));
    CHECK(above.y < centre.y);    // y-up world -> y-down screen
    CHECK(right.x > centre.x);    // x is not flipped
}

TEST_CASE("camera zoom scales distances from the camera's position") {
    Camera camera;
    camera.SetViewportSize(Vec2{800.0f, 600.0f});
    camera.SetPosition(Vec2{0.0f, 0.0f});

    camera.SetZoom(1.0f);
    const Vec2 atOne = camera.WorldToScreen(Vec2{100.0f, 0.0f});
    camera.SetZoom(2.0f);
    const Vec2 atTwo = camera.WorldToScreen(Vec2{100.0f, 0.0f});

    CHECK((atTwo.x - 400.0f) == doctest::Approx(2.0f * (atOne.x - 400.0f)));
}

TEST_CASE("VisibleBounds covers the viewport and shrinks as zoom increases") {
    Camera camera;
    camera.SetViewportSize(Vec2{800.0f, 600.0f});
    camera.SetPosition(Vec2{0.0f, 0.0f});

    camera.SetZoom(1.0f);
    const AABB wide = camera.VisibleBounds();
    CHECK(wide.Size().x == doctest::Approx(800.0f));
    CHECK(wide.Size().y == doctest::Approx(600.0f));

    camera.SetZoom(2.0f);
    const AABB narrow = camera.VisibleBounds();
    CHECK(narrow.Size().x == doctest::Approx(400.0f));
}
