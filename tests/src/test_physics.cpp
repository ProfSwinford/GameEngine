// ============================================================================
//  test_physics.cpp - the shape maths, and the physics built on top of it.
//
//  THE GEOMETRY IS TESTED FIRST AND HARDEST, because everything else in the
//  physics system is a consequence of it. A collider is a shape plus a layer;
//  a contact is what Collide() returned; a resolved overlap is that contact's
//  normal times its separation. If the maths is wrong, every layer above it
//  is wrong in a way that looks like bad gameplay rather than bad arithmetic.
//
//  The normal convention is asserted over and over on purpose: Collide(a, b)
//  returns a normal pointing FROM A TOWARD B. A sign error there does not
//  crash - it pulls things together instead of apart, and the symptom is
//  objects that snap to each other like magnets.
// ============================================================================

#include <doctest/doctest.h>

#include <engine/math/Overlap.h>

#include <algorithm>
#include <cmath>

using namespace eng;

namespace {

// A point, expressed as the shape the collision routine wants. Handy for
// asking "is this spot inside that shape?" without a second code path.
ConvexShape PointShape(Vec2 at) { return ToConvex(Circle{at, 0.0f}); }

// eng::kPi comes from math/Vec2.h; kTwoPi and the degree conversions are there too.


} // namespace

// ---------------------------------------------------------------------------
//  Circles - the case simple enough to check by hand
// ---------------------------------------------------------------------------

TEST_CASE("two circles apart report the gap between them") {
    const Contact c = Collide(ToConvex(Circle{Vec2{0.0f, 0.0f}, 1.0f}),
                              ToConvex(Circle{Vec2{3.0f, 0.0f}, 1.0f}));

    CHECK(!c.touching);
    CHECK(c.separation == doctest::Approx(1.0f));       // 3 - 1 - 1
    CHECK(c.normal.x == doctest::Approx(1.0f));         // A -> B is +x
    CHECK(c.normal.y == doctest::Approx(0.0f));
}

TEST_CASE("two overlapping circles report a negative separation") {
    const Contact c = Collide(ToConvex(Circle{Vec2{0.0f, 0.0f}, 1.0f}),
                              ToConvex(Circle{Vec2{1.5f, 0.0f}, 1.0f}));

    CHECK(c.touching);
    CHECK(c.separation == doctest::Approx(-0.5f));      // 1.5 - 2
    CHECK(c.normal.x == doctest::Approx(1.0f));
}

TEST_CASE("the normal flips when the arguments are swapped") {
    // Not a tautology worth skipping: the two sides take different branches
    // through the axis scan, and an asymmetry here is exactly the bug that
    // makes one of two colliding objects get pushed and the other not.
    const ConvexShape left  = ToConvex(Circle{Vec2{0.0f, 0.0f}, 1.0f});
    const ConvexShape right = ToConvex(Circle{Vec2{1.5f, 0.0f}, 1.0f});

    const Contact ab = Collide(left, right);
    const Contact ba = Collide(right, left);

    CHECK(ab.separation == doctest::Approx(ba.separation));
    CHECK(ab.normal.x == doctest::Approx(-ba.normal.x));
}

TEST_CASE("circles touching at exactly one point count as touching") {
    // The engine's stated rule, asserted where it would otherwise drift.
    const Contact c = Collide(ToConvex(Circle{Vec2{0.0f, 0.0f}, 1.0f}),
                              ToConvex(Circle{Vec2{2.0f, 0.0f}, 1.0f}));
    CHECK(c.touching);
    CHECK(c.separation == doctest::Approx(0.0f));
}

// ---------------------------------------------------------------------------
//  Boxes, and the thing the old collision system could not do
// ---------------------------------------------------------------------------

TEST_CASE("a box and a circle off one corner do not touch") {
    // The blind spot in the shortcut this engine deliberately avoids: a circle
    // near a box's corner is further away than either axis alone suggests.
    const ConvexShape box = ToConvex(AABB::FromMinMax(Vec2{-1.0f, -1.0f},
                                                      Vec2{1.0f, 1.0f}));
    const ConvexShape circle = ToConvex(Circle{Vec2{1.6f, 1.6f}, 0.5f});

    CHECK(!Collide(box, circle).touching);
}

TEST_CASE("a rotated box is tested as a rotated box, not as the box around it") {
    // THE HEADLINE CASE for oriented boxes. A 2x2 square turned 45 degrees has
    // its corners on the axes at distance sqrt(2), so the upright box that
    // SURROUNDS it spans -1.414..1.414 on both axes.
    //
    // The point (1.2, 1.2) is inside that surrounding box and outside the real
    // rotated square, whose edge is the line x + y = sqrt(2) = 1.414. Before
    // oriented boxes existed this reported a hit.
    const OBB turned{Vec2{0.0f, 0.0f}, Vec2{1.0f, 1.0f}, kPi * 0.25f};
    const ConvexShape shape = ToConvex(turned);

    CHECK(!Collide(shape, PointShape(Vec2{1.2f, 1.2f})).touching);

    // ...while a point just inside that same diagonal edge does hit, which is
    // what stops this test passing for the trivial reason that nothing hits.
    CHECK(Collide(shape, PointShape(Vec2{0.6f, 0.6f})).touching);

    // And the corner really is out at sqrt(2) along the axis.
    CHECK(Collide(shape, PointShape(Vec2{1.3f, 0.0f})).touching);
    CHECK(!Collide(shape, PointShape(Vec2{1.5f, 0.0f})).touching);
}

TEST_CASE("two overlapping boxes are pushed apart the short way") {
    // Overlapping 1 unit on x and 0.2 on y: the way out is along y, because
    // that is the shallower direction. Picking the deeper axis would shove the
    // object the long way through the obstacle.
    const ConvexShape a = ToConvex(AABB::FromMinMax(Vec2{0.0f, 0.0f}, Vec2{2.0f, 2.0f}));
    const ConvexShape b = ToConvex(AABB::FromMinMax(Vec2{1.0f, 1.8f}, Vec2{3.0f, 3.8f}));

    const Contact c = Collide(a, b);
    CHECK(c.touching);
    CHECK(c.separation == doctest::Approx(-0.2f));
    CHECK(c.normal.x == doctest::Approx(0.0f));
    CHECK(c.normal.y == doctest::Approx(1.0f));   // A moves -y, B moves +y
}

// ---------------------------------------------------------------------------
//  Capsules
// ---------------------------------------------------------------------------

TEST_CASE("a capsule is a segment with thickness, including its end caps") {
    const ConvexShape capsule =
        ToConvex(Capsule{Vec2{0.0f, 0.0f}, Vec2{2.0f, 0.0f}, 0.5f});

    // Beside the middle: inside at 0.4 out, outside at 0.6.
    CHECK(Collide(capsule, PointShape(Vec2{1.0f, 0.4f})).touching);
    CHECK(!Collide(capsule, PointShape(Vec2{1.0f, 0.6f})).touching);

    // Off the rounded end, measured from the END of the core rather than from
    // the middle - which is what makes it a capsule and not a box.
    CHECK(Collide(capsule, PointShape(Vec2{-0.4f, 0.0f})).touching);
    CHECK(!Collide(capsule, PointShape(Vec2{-0.6f, 0.0f})).touching);

    // Diagonally off the end cap: 0.45 on each axis is 0.636 away, so out.
    CHECK(!Collide(capsule, PointShape(Vec2{-0.45f, 0.45f})).touching);
}

TEST_CASE("a capsule with both ends at one place is a circle") {
    const ConvexShape degenerate =
        ToConvex(Capsule{Vec2{1.0f, 1.0f}, Vec2{1.0f, 1.0f}, 2.0f});

    CHECK(degenerate.count == 1);
    CHECK(Collide(degenerate, PointShape(Vec2{2.9f, 1.0f})).touching);
    CHECK(!Collide(degenerate, PointShape(Vec2{3.1f, 1.0f})).touching);
}

// ---------------------------------------------------------------------------
//  Polygons
// ---------------------------------------------------------------------------

TEST_CASE("a triangle contains and excludes the right points") {
    // Anticlockwise, as the winding rule requires.
    const Vec2        tri[3] = {Vec2{0.0f, 0.0f}, Vec2{4.0f, 0.0f}, Vec2{0.0f, 4.0f}};
    const ConvexShape shape  = ToConvex(tri, 3);

    CHECK(Collide(shape, PointShape(Vec2{0.5f, 0.5f})).touching);   // well inside
    CHECK(!Collide(shape, PointShape(Vec2{3.0f, 3.0f})).touching);  // past the hypotenuse
    CHECK(!Collide(shape, PointShape(Vec2{-0.5f, 1.0f})).touching); // outside an edge
}

TEST_CASE("a polygon with too many points is trimmed rather than overflowing") {
    Vec2 many[16];
    for (int i = 0; i < 16; ++i) {
        const float angle = (static_cast<float>(i) / 16.0f) * 2.0f * kPi;
        many[i] = Vec2{std::cos(angle), std::sin(angle)};
    }
    const ConvexShape shape = ToConvex(many, 16);
    CHECK(shape.count == ConvexShape::kMaxPoints);
}

// ---------------------------------------------------------------------------
//  Closest point
// ---------------------------------------------------------------------------

TEST_CASE("the closest point on a circle is on its surface") {
    const ConvexShape circle = ToConvex(Circle{Vec2{0.0f, 0.0f}, 2.0f});
    const Vec2        p      = ClosestPoint(circle, Vec2{5.0f, 0.0f});
    CHECK(p.x == doctest::Approx(2.0f));
    CHECK(p.y == doctest::Approx(0.0f));
}

TEST_CASE("the closest point on a box is on its edge") {
    const ConvexShape box =
        ToConvex(AABB::FromMinMax(Vec2{-1.0f, -1.0f}, Vec2{1.0f, 1.0f}));
    const Vec2 p = ClosestPoint(box, Vec2{4.0f, 0.5f});
    CHECK(p.x == doctest::Approx(1.0f));
    CHECK(p.y == doctest::Approx(0.5f));
}

TEST_CASE("closest points between two crossing segments meet at the crossing") {
    Vec2 onFirst{};
    Vec2 onSecond{};
    ClosestPointsBetweenSegments(Segment{Vec2{-1.0f, 0.0f}, Vec2{1.0f, 0.0f}},
                                 Segment{Vec2{0.0f, -1.0f}, Vec2{0.0f, 1.0f}},
                                 onFirst, onSecond);
    CHECK(Distance(onFirst, onSecond) == doctest::Approx(0.0f));
    CHECK(onFirst.x == doctest::Approx(0.0f));
}

TEST_CASE("closest points between parallel segments are a clean perpendicular") {
    // The degenerate case: every pair along the overlap is equally close, so
    // the only thing worth asserting is that the distance is right and the
    // routine did not divide by zero and hand back a NaN.
    Vec2 onFirst{};
    Vec2 onSecond{};
    ClosestPointsBetweenSegments(Segment{Vec2{0.0f, 0.0f}, Vec2{4.0f, 0.0f}},
                                 Segment{Vec2{0.0f, 3.0f}, Vec2{4.0f, 3.0f}},
                                 onFirst, onSecond);
    CHECK(Distance(onFirst, onSecond) == doctest::Approx(3.0f));
    CHECK(!std::isnan(onFirst.x));
    CHECK(!std::isnan(onSecond.x));
}

// ---------------------------------------------------------------------------
//  Rays
// ---------------------------------------------------------------------------

TEST_CASE("a ray hits a box on its near face") {
    RaycastHit  hit;
    const AABB  box = AABB::FromMinMax(Vec2{-1.0f, -1.0f}, Vec2{1.0f, 1.0f});
    const Ray   ray{Vec2{-5.0f, 0.0f}, Vec2{1.0f, 0.0f}, 10.0f};

    REQUIRE(Raycast(ray, box, hit));
    CHECK(hit.distance == doctest::Approx(4.0f));
    CHECK(hit.point.x == doctest::Approx(-1.0f));
    CHECK(hit.normal.x == doctest::Approx(-1.0f));
    CHECK(hit.fraction == doctest::Approx(0.4f));
}

TEST_CASE("a ray that stops short of a box misses it") {
    RaycastHit hit;
    const AABB box = AABB::FromMinMax(Vec2{-1.0f, -1.0f}, Vec2{1.0f, 1.0f});
    CHECK(!Raycast(Ray{Vec2{-5.0f, 0.0f}, Vec2{1.0f, 0.0f}, 3.0f}, box, hit));
}

TEST_CASE("a ray hits a circle at the right distance") {
    RaycastHit hit;
    const ConvexShape circle = ToConvex(Circle{Vec2{0.0f, 0.0f}, 1.0f});

    REQUIRE(Raycast(Ray{Vec2{-5.0f, 0.0f}, Vec2{1.0f, 0.0f}, 10.0f}, circle, hit));
    CHECK(hit.distance == doctest::Approx(4.0f));
    CHECK(hit.normal.x == doctest::Approx(-1.0f));
}

TEST_CASE("a ray passing wide of a circle misses") {
    RaycastHit hit;
    const ConvexShape circle = ToConvex(Circle{Vec2{0.0f, 0.0f}, 1.0f});
    CHECK(!Raycast(Ray{Vec2{-5.0f, 2.0f}, Vec2{1.0f, 0.0f}, 10.0f}, circle, hit));
}

TEST_CASE("a ray starting inside a shape hits at zero, facing back") {
    // The decision recorded in Overlap.h: starting inside is a hit, so that
    // "am I standing in something?" is a question a raycast can answer.
    RaycastHit hit;
    const ConvexShape box =
        ToConvex(AABB::FromMinMax(Vec2{-1.0f, -1.0f}, Vec2{1.0f, 1.0f}));

    REQUIRE(Raycast(Ray{Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f}, 10.0f}, box, hit));
    CHECK(hit.distance == doctest::Approx(0.0f));
    CHECK(hit.normal.x == doctest::Approx(-1.0f));   // back along the ray
}

TEST_CASE("a ray hits a polygon") {
    RaycastHit hit;
    const Vec2        tri[3] = {Vec2{0.0f, 0.0f}, Vec2{4.0f, 0.0f}, Vec2{0.0f, 4.0f}};
    const ConvexShape shape  = ToConvex(tri, 3);

    REQUIRE(Raycast(Ray{Vec2{-3.0f, 1.0f}, Vec2{1.0f, 0.0f}, 10.0f}, shape, hit));
    CHECK(hit.distance == doctest::Approx(3.0f));
    CHECK(hit.normal.x == doctest::Approx(-1.0f));
}

TEST_CASE("a ray hits a capsule on its side and on its cap") {
    const ConvexShape capsule =
        ToConvex(Capsule{Vec2{0.0f, 0.0f}, Vec2{4.0f, 0.0f}, 1.0f});

    RaycastHit side;
    REQUIRE(Raycast(Ray{Vec2{2.0f, -5.0f}, Vec2{0.0f, 1.0f}, 10.0f}, capsule, side));
    CHECK(side.distance == doctest::Approx(4.0f).epsilon(0.02));

    RaycastHit cap;
    REQUIRE(Raycast(Ray{Vec2{-5.0f, 0.0f}, Vec2{1.0f, 0.0f}, 10.0f}, capsule, cap));
    CHECK(cap.distance == doctest::Approx(4.0f).epsilon(0.02));

    RaycastHit miss;
    CHECK(!Raycast(Ray{Vec2{2.0f, -5.0f}, Vec2{0.0f, 1.0f}, 3.0f}, capsule, miss));
}

// ===========================================================================
//  The collider components, the rigidbody, and the queries.
//
//  These need a SCENE, because a collider's shape comes from its entity's
//  world transform and the queries walk the live collider list. They do not
//  need a window, a renderer or a game loop - Scene and the collision system
//  stand on their own, which is what makes this a unit test.
// ===========================================================================

#include <engine/Engine.h>

namespace {

// A scene that cleans up after itself.
//
// The collision system and the collider list are GLOBAL - one world - so a
// case that left entities behind would change the answers of every case after
// it, and which ones those are depends on link order. That is the worst way
// to find out a test suite is order-dependent.
struct SceneFixture {
    eng::Scene scene;

    SceneFixture() {
        // THE COMPONENT FACTORY HAS TO BE FILLED IN BY HAND HERE.
        //
        // Engine::Init normally does this, and these tests deliberately do not
        // start the engine - there is no window and no renderer to start. So
        // AddComponent would hand back a null pointer for every type, which is
        // a segfault one line later rather than a readable failure.
        //
        // Registering is idempotent (the table is keyed by name) so doing it
        // per fixture rather than once globally costs nothing and keeps the
        // setup in the one place that needs it.
        eng::ComponentFactory::RegisterBuiltins();
        eng::CollisionSystem::RegisterComponentTypes();
        eng::RigidbodySystem::RegisterComponentTypes();

        eng::Scene::SetActive(&scene);
        eng::Physics2D::SetGravity(eng::Vec2{0.0f, -1000.0f});
        eng::CollisionSystem::SetSolverIterations(4);
    }

    ~SceneFixture() {
        scene.Unload();
        eng::Scene::SetActive(nullptr);
        eng::CollisionSystem::Clear();
        eng::RigidbodySystem::Clear();
    }

    eng::Entity* Add(const char* name, eng::Vec2 at) {
        eng::Entity* entity = scene.Get(scene.CreateEntity(name));
        entity->Transform().SetLocalPosition(at);
        return entity;
    }
};

// One simulation step: integrate, then collide. The same two stages
// Engine::Simulate runs, without needing the engine.
void Step(float dt = 1.0f / 60.0f) {
    eng::RigidbodySystem bodies;
    bodies.Update(dt);

    eng::CollisionSystem collision;
    collision.Update(dt);
}

// Attaching a collider and setting its size, which every case below wants.
eng::BoxColliderComponent* AddBox(eng::Entity& entity, eng::Vec2 halfExtents) {
    auto* box = static_cast<eng::BoxColliderComponent*>(
        entity.AddComponent(eng::BoxColliderComponent::kTypeName));
    box->SetHalfExtents(halfExtents);
    return box;
}

eng::Rigidbody2DComponent* AddBody(eng::Entity& entity) {
    return static_cast<eng::Rigidbody2DComponent*>(
        entity.AddComponent(eng::Rigidbody2DComponent::kTypeName));
}

} // namespace

// ---------------------------------------------------------------------------
//  The collider components
// ---------------------------------------------------------------------------

TEST_CASE("a box collider's bounds follow its entity's transform") {
    SceneFixture fixture;
    eng::Entity* entity = fixture.Add("Box", eng::Vec2{100.0f, 50.0f});
    AddBox(*entity, eng::Vec2{10.0f, 4.0f});

    const eng::AABB bounds = eng::FindCollider(*entity)->WorldBounds();
    CHECK(bounds.min.x == doctest::Approx(90.0f));
    CHECK(bounds.max.x == doctest::Approx(110.0f));
    CHECK(bounds.min.y == doctest::Approx(46.0f));
    CHECK(bounds.max.y == doctest::Approx(54.0f));
}

TEST_CASE("a rotated box collider is no longer the box around it") {
    // The same claim as the pure-geometry test, but through the component - so
    // it covers the transform plumbing as well as the maths. A 45-degree
    // rotation used to inflate the tested shape by about 40%.
    SceneFixture fixture;
    eng::Entity* entity = fixture.Add("Turned", eng::Vec2{0.0f, 0.0f});
    entity->Transform().SetLocalRotation(eng::kPi * 0.25f);
    eng::BoxColliderComponent* box = AddBox(*entity, eng::Vec2{1.0f, 1.0f});

    // Inside the enlarged upright box, outside the real rotated square.
    CHECK(!box->OverlapPoint(eng::Vec2{1.2f, 1.2f}));
    CHECK(box->OverlapPoint(eng::Vec2{1.3f, 0.0f}));
}

TEST_CASE("the legacy AABBColliderComponent name still loads") {
    SceneFixture fixture;   // registers the component types

    // Every scene file written before boxes could rotate names the component
    // this way. If this ever fails, those files stop opening.
    CHECK(eng::ComponentFactory::IsRegistered(
        eng::BoxColliderComponent::kLegacyTypeName));

    const auto made =
        eng::ComponentFactory::Create(eng::BoxColliderComponent::kLegacyTypeName);
    REQUIRE(made != nullptr);

    // It is the SAME component, so it reports the new name - which is what
    // makes Find<BoxColliderComponent>() work on an old scene.
    CHECK(std::string_view(made->TypeName()) == eng::BoxColliderComponent::kTypeName);
}

TEST_CASE("FindCollider finds a collider of any shape") {
    // The gap that five shapes opened: Entity::Find<T> matches a type NAME,
    // so it cannot answer "has this got any collider at all".
    SceneFixture fixture;

    eng::Entity* entity = fixture.Add("Capsule", eng::Vec2{0.0f, 0.0f});
    CHECK(eng::FindCollider(*entity) == nullptr);

    entity->AddComponent(eng::CapsuleColliderComponent::kTypeName);
    eng::ColliderComponent* found = eng::FindCollider(*entity);
    REQUIRE(found != nullptr);
    CHECK(found->Shape() == eng::ColliderShape::Capsule);
}

TEST_CASE("an edge collider produces one shape per segment") {
    SceneFixture fixture;
    eng::Entity* entity = fixture.Add("Ground", eng::Vec2{0.0f, 0.0f});

    auto* edge = static_cast<eng::EdgeColliderComponent*>(
        entity->AddComponent(eng::EdgeColliderComponent::kTypeName));
    edge->SetPoints({eng::Vec2{0.0f, 0.0f}, eng::Vec2{10.0f, 0.0f},
                     eng::Vec2{20.0f, 5.0f}, eng::Vec2{30.0f, 5.0f}});

    eng::ConvexShape shapes[eng::ColliderComponent::kMaxWorldShapes];
    CHECK(edge->WorldShapes(shapes, eng::ColliderComponent::kMaxWorldShapes) == 3);

    // Closed, it joins back to the start and gains a segment.
    edge->SetLoop(true);
    CHECK(edge->WorldShapes(shapes, eng::ColliderComponent::kMaxWorldShapes) == 4);
}

TEST_CASE("a polygon collider reports a dented outline as not convex") {
    SceneFixture fixture;
    eng::Entity* entity = fixture.Add("Poly", eng::Vec2{0.0f, 0.0f});

    auto* polygon = static_cast<eng::PolygonColliderComponent*>(
        entity->AddComponent(eng::PolygonColliderComponent::kTypeName));

    polygon->SetPoints({eng::Vec2{0.0f, 0.0f}, eng::Vec2{4.0f, 0.0f},
                        eng::Vec2{4.0f, 4.0f}, eng::Vec2{0.0f, 4.0f}});
    CHECK(polygon->IsConvex());

    // The same square with one corner pushed inward.
    polygon->SetPoints({eng::Vec2{0.0f, 0.0f}, eng::Vec2{4.0f, 0.0f},
                        eng::Vec2{1.0f, 1.0f}, eng::Vec2{0.0f, 4.0f}});
    CHECK(!polygon->IsConvex());
}

TEST_CASE("a capsule as wide as it is tall is a circle") {
    SceneFixture fixture;
    eng::Entity* entity = fixture.Add("Round", eng::Vec2{0.0f, 0.0f});
    auto*        capsule = static_cast<eng::CapsuleColliderComponent*>(
        entity->AddComponent(eng::CapsuleColliderComponent::kTypeName));
    capsule->SetSize(eng::Vec2{20.0f, 20.0f});

    eng::ConvexShape shapes[eng::ColliderComponent::kMaxWorldShapes];
    REQUIRE(capsule->WorldShapes(shapes, eng::ColliderComponent::kMaxWorldShapes) == 1);
    CHECK(shapes[0].count == 1);                           // one point
    CHECK(shapes[0].radius == doctest::Approx(10.0f));     // and a radius
}

TEST_CASE("a disabled collider takes part in nothing") {
    SceneFixture fixture;
    eng::Entity* entity = fixture.Add("Box", eng::Vec2{0.0f, 0.0f});
    eng::BoxColliderComponent* box = AddBox(*entity, eng::Vec2{10.0f, 10.0f});

    CHECK(eng::Physics2D::OverlapPoint(eng::Vec2{0.0f, 0.0f}) != nullptr);

    box->SetEnabled(false);
    CHECK(eng::Physics2D::OverlapPoint(eng::Vec2{0.0f, 0.0f}) == nullptr);
}

// ---------------------------------------------------------------------------
//  The rigidbody
// ---------------------------------------------------------------------------

TEST_CASE("inverse mass is zero for anything that cannot be pushed") {
    // The single decision the whole solver rests on. If a static body ever
    // reports a non-zero inverse mass, scenery starts being shoved about by
    // whatever walks into it.
    SceneFixture fixture;
    eng::Entity* entity = fixture.Add("Body", eng::Vec2{0.0f, 0.0f});
    eng::Rigidbody2DComponent* body = AddBody(*entity);
    body->SetMass(4.0f);

    body->SetBodyType(eng::RigidbodyType::Dynamic);
    CHECK(body->InverseMass() == doctest::Approx(0.25f));

    body->SetBodyType(eng::RigidbodyType::Kinematic);
    CHECK(body->InverseMass() == doctest::Approx(0.0f));

    body->SetBodyType(eng::RigidbodyType::Static);
    CHECK(body->InverseMass() == doctest::Approx(0.0f));
}

TEST_CASE("gravity is an acceleration, so mass does not change the fall") {
    // Galileo, asserted. Two bodies of very different mass must fall together
    // - if they do not, gravity has been applied as a force somewhere.
    SceneFixture fixture;

    eng::Entity* light = fixture.Add("Light", eng::Vec2{0.0f, 0.0f});
    AddBody(*light)->SetMass(0.5f);

    eng::Entity* heavy = fixture.Add("Heavy", eng::Vec2{1000.0f, 0.0f});
    AddBody(*heavy)->SetMass(500.0f);

    for (int i = 0; i < 30; ++i) {
        Step();
    }

    CHECK(light->Transform().WorldPosition().y < -1.0f);   // it did fall
    CHECK(light->Transform().WorldPosition().y ==
          doctest::Approx(heavy->Transform().WorldPosition().y));
}

TEST_CASE("gravity scale makes a body float, fall or rise") {
    SceneFixture fixture;

    eng::Entity* floating = fixture.Add("Float", eng::Vec2{0.0f, 0.0f});
    AddBody(*floating)->SetGravityScale(0.0f);

    eng::Entity* falling = fixture.Add("Fall", eng::Vec2{100.0f, 0.0f});
    AddBody(*falling)->SetGravityScale(1.0f);

    eng::Entity* rising = fixture.Add("Rise", eng::Vec2{200.0f, 0.0f});
    AddBody(*rising)->SetGravityScale(-1.0f);

    for (int i = 0; i < 20; ++i) {
        Step();
    }

    CHECK(floating->Transform().WorldPosition().y == doctest::Approx(0.0f));
    CHECK(falling->Transform().WorldPosition().y < -1.0f);
    CHECK(rising->Transform().WorldPosition().y > 1.0f);
}

TEST_CASE("an impulse changes velocity at once and a force waits for the step") {
    SceneFixture fixture;
    eng::Entity* entity = fixture.Add("Body", eng::Vec2{0.0f, 0.0f});
    eng::Rigidbody2DComponent* body = AddBody(*entity);
    body->SetMass(2.0f);
    body->SetGravityScale(0.0f);

    body->AddForce(eng::Vec2{10.0f, 0.0f}, eng::ForceMode::Impulse);
    CHECK(body->Velocity().x == doctest::Approx(5.0f));   // 10 / 2, right now

    body->SetVelocity(eng::Vec2{0.0f, 0.0f});
    body->AddForce(eng::Vec2{10.0f, 0.0f}, eng::ForceMode::Force);
    CHECK(body->Velocity().x == doctest::Approx(0.0f));   // nothing yet

    Step();
    // 10 N on 2 kg for 1/60 s: 5 * (1/60).
    CHECK(body->Velocity().x == doctest::Approx(5.0f / 60.0f));
}

TEST_CASE("a force is spent at the end of the step that used it") {
    // Unity's rule. A continuous push has to be applied every step, which is
    // why AddForce belongs in OnFixedUpdate - and if the accumulator were not
    // cleared, one call would accelerate a body forever.
    SceneFixture fixture;
    eng::Entity* entity = fixture.Add("Body", eng::Vec2{0.0f, 0.0f});
    eng::Rigidbody2DComponent* body = AddBody(*entity);
    body->SetGravityScale(0.0f);

    body->AddForce(eng::Vec2{60.0f, 0.0f}, eng::ForceMode::Force);
    Step();
    const float after1 = body->Velocity().x;
    Step();
    const float after2 = body->Velocity().x;

    CHECK(after1 > 0.0f);
    CHECK(after2 == doctest::Approx(after1));   // not still accelerating
}

TEST_CASE("linear drag bleeds motion away and never reverses it") {
    // The reason drag is a division rather than a subtraction: a large drag or
    // a long step would make the obvious `v -= v * drag * dt` overshoot past
    // zero, and the body would accelerate backwards.
    SceneFixture fixture;
    eng::Entity* entity = fixture.Add("Body", eng::Vec2{0.0f, 0.0f});
    eng::Rigidbody2DComponent* body = AddBody(*entity);
    body->SetGravityScale(0.0f);
    body->SetLinearDrag(50.0f);          // absurdly high on purpose
    body->SetVelocity(eng::Vec2{100.0f, 0.0f});

    for (int i = 0; i < 60; ++i) {
        Step();
        CHECK(body->Velocity().x >= 0.0f);   // never flips sign
    }
    CHECK(body->Velocity().x < 1.0f);        // but does stop
}

// ---------------------------------------------------------------------------
//  Contacts: resolution, triggers and layers
// ---------------------------------------------------------------------------

TEST_CASE("a falling body comes to rest on a static one") {
    SceneFixture fixture;

    eng::Entity* ground = fixture.Add("Ground", eng::Vec2{0.0f, 0.0f});
    AddBox(*ground, eng::Vec2{200.0f, 8.0f});

    eng::Entity* faller = fixture.Add("Faller", eng::Vec2{0.0f, 120.0f});
    AddBox(*faller, eng::Vec2{8.0f, 8.0f});
    eng::Rigidbody2DComponent* body = AddBody(*faller);
    body->SetFreezeRotation(true);

    for (int i = 0; i < 180; ++i) {
        Step();
    }

    // Ground top is y = 8, the faller is 8 from its centre to its base, so it
    // should settle at 16 - less the sliver of overlap the solver leaves on
    // purpose so that a resting contact does not buzz. See CorrectPosition.
    const float restingY = faller->Transform().WorldPosition().y;
    CHECK(restingY < 16.0f);
    CHECK(restingY > 15.5f);

    // AND IT ACTUALLY STOPPED. Without this the test passes for a body that is
    // still slowly sinking through the floor.
    CHECK(std::fabs(body->Velocity().y) < 1.0f);
}

TEST_CASE("a static collider is never moved by what lands on it") {
    SceneFixture fixture;

    eng::Entity* ground = fixture.Add("Ground", eng::Vec2{0.0f, 0.0f});
    AddBox(*ground, eng::Vec2{200.0f, 8.0f});

    eng::Entity* faller = fixture.Add("Faller", eng::Vec2{0.0f, 40.0f});
    AddBox(*faller, eng::Vec2{8.0f, 8.0f});
    AddBody(*faller);

    for (int i = 0; i < 120; ++i) {
        Step();
    }

    CHECK(ground->Transform().WorldPosition().y == doctest::Approx(0.0f));
    CHECK(ground->Transform().WorldPosition().x == doctest::Approx(0.0f));
}

TEST_CASE("a bouncy body returns and a dead one does not") {
    SceneFixture fixture;

    eng::Entity* ground = fixture.Add("Ground", eng::Vec2{0.0f, 0.0f});
    AddBox(*ground, eng::Vec2{400.0f, 8.0f});

    const auto dropWithBounciness = [&fixture](const char* name, float x,
                                               float bounciness) {
        eng::Entity* entity = fixture.Add(name, eng::Vec2{x, 150.0f});
        eng::BoxColliderComponent* box = AddBox(*entity, eng::Vec2{8.0f, 8.0f});
        eng::PhysicsMaterial2D     material = box->Material();
        material.bounciness = bounciness;
        box->SetMaterial(material);
        AddBody(*entity)->SetFreezeRotation(true);
        return entity;
    };

    eng::Entity* dead   = dropWithBounciness("Dead", 0.0f, 0.0f);
    eng::Entity* bouncy = dropWithBounciness("Bouncy", 100.0f, 0.9f);

    // Long enough to land, and for the bouncy one to be on its way back up.
    float deadPeak   = -1000.0f;
    float bouncyPeak = -1000.0f;
    bool  landed     = false;
    for (int i = 0; i < 90; ++i) {
        Step();
        if (dead->Transform().WorldPosition().y < 20.0f) {
            landed = true;
        }
        if (landed) {
            deadPeak   = std::max(deadPeak, dead->Transform().WorldPosition().y);
            bouncyPeak = std::max(bouncyPeak, bouncy->Transform().WorldPosition().y);
        }
    }

    REQUIRE(landed);
    CHECK(bouncyPeak > deadPeak + 20.0f);   // it came back up, the other did not
}

TEST_CASE("a trigger reports an overlap and does not push") {
    SceneFixture fixture;

    eng::Entity* trigger = fixture.Add("Trigger", eng::Vec2{0.0f, 0.0f});
    AddBox(*trigger, eng::Vec2{50.0f, 50.0f})->SetTrigger(true);

    eng::Entity* mover = fixture.Add("Mover", eng::Vec2{0.0f, 0.0f});
    AddBox(*mover, eng::Vec2{5.0f, 5.0f});
    AddBody(*mover)->SetGravityScale(0.0f);

    Step();

    // They overlap, so the pair is live...
    CHECK(eng::CollisionSystem::ActivePairCount() == 1);

    // ...and the mover was not pushed out of it.
    CHECK(mover->Transform().WorldPosition().x == doctest::Approx(0.0f));
    CHECK(mover->Transform().WorldPosition().y == doctest::Approx(0.0f));
}

TEST_CASE("two solid colliders with no rigidbody report but do not move") {
    // Unity's rule, and worth asserting because it is the usual explanation
    // for "my player walks through walls": a collider and no rigidbody.
    SceneFixture fixture;

    eng::Entity* a = fixture.Add("A", eng::Vec2{0.0f, 0.0f});
    AddBox(*a, eng::Vec2{10.0f, 10.0f});

    eng::Entity* b = fixture.Add("B", eng::Vec2{5.0f, 0.0f});
    AddBox(*b, eng::Vec2{10.0f, 10.0f});

    Step();

    CHECK(eng::CollisionSystem::ActivePairCount() == 1);   // reported
    CHECK(a->Transform().WorldPosition().x == doctest::Approx(0.0f));
    CHECK(b->Transform().WorldPosition().x == doctest::Approx(5.0f));
}

TEST_CASE("one-sided interest is not enough") {
    // The engine's stated rule: BOTH sides have to care. A one-sided event is
    // the kind of bug where the first hour goes on not believing it.
    SceneFixture fixture;

    eng::Entity*               a    = fixture.Add("A", eng::Vec2{0.0f, 0.0f});
    eng::BoxColliderComponent* boxA = AddBox(*a, eng::Vec2{10.0f, 10.0f});
    boxA->SetLayer("Player");
    boxA->SetCollidesWith({"Wall"});

    eng::Entity*               b    = fixture.Add("B", eng::Vec2{0.0f, 0.0f});
    eng::BoxColliderComponent* boxB = AddBox(*b, eng::Vec2{10.0f, 10.0f});
    boxB->SetLayer("Wall");

    // Mutual: a pair.
    boxB->SetCollidesWith({"Player"});
    Step();
    CHECK(eng::CollisionSystem::ActivePairCount() == 1);

    // One-sided: none.
    boxB->SetCollidesWith({"Enemy"});
    Step();
    CHECK(eng::CollisionSystem::ActivePairCount() == 0);
}

TEST_CASE("the broad phase skips pairs that are nowhere near each other") {
    // Spread far apart on x, so the sweep's early exit should reject every
    // pair before the shape test is reached.
    SceneFixture fixture;

    for (int i = 0; i < 40; ++i) {
        // A DISTINCT NAME EACH, which this loop used not to bother with. Names
        // are an identity in this engine - parents and entity references are
        // both recorded by name - so forty live entities called "Spread" is
        // forty things claiming to be one, and Scene::CreateEntity now says so
        // at Error level. Nothing here depended on the collision; the loop
        // simply had no reason to invent names and now has one.
        const std::string name = "Spread" + std::to_string(i);
        eng::Entity*      entity =
            fixture.Add(name.c_str(), eng::Vec2{static_cast<float>(i) * 500.0f, 0.0f});
        AddBox(*entity, eng::Vec2{10.0f, 10.0f});
    }

    Step();

    CHECK(eng::CollisionSystem::BruteForcePairs() == 40 * 39 / 2);   // 780
    CHECK(eng::CollisionSystem::ActivePairCount() == 0);
    CHECK(eng::CollisionSystem::NarrowPhaseTests() == 0);

    // THE CLAIM THE SWEEP ACTUALLY MAKES, and the reason this is counted
    // rather than derived.
    //
    // An earlier version of this test asserted only that the narrow phase ran
    // zero times - which passed with the sweep's early exit DELETED, because
    // the cheap box test rejects these pairs too. It was a test of the box
    // test wearing a broad phase's name.
    //
    // Sorted on the left edge and spread 500 apart, each outer item should
    // reject its first neighbour and break immediately: about one comparison
    // each rather than all 780.
    CHECK(eng::CollisionSystem::BroadPhasePairs() < 60);
}

// ---------------------------------------------------------------------------
//  The queries
// ---------------------------------------------------------------------------

TEST_CASE("a raycast finds the nearest collider and its surface normal") {
    SceneFixture fixture;

    eng::Entity* nearWall = fixture.Add("Near", eng::Vec2{100.0f, 0.0f});
    AddBox(*nearWall, eng::Vec2{10.0f, 10.0f});

    eng::Entity* farWall = fixture.Add("Far", eng::Vec2{300.0f, 0.0f});
    AddBox(*farWall, eng::Vec2{10.0f, 10.0f});

    const eng::RaycastHit2D hit =
        eng::Physics2D::Raycast(eng::Vec2{0.0f, 0.0f}, eng::Vec2{1.0f, 0.0f}, 500.0f);

    REQUIRE(hit);
    CHECK(hit.entity == nearWall->Id());
    CHECK(hit.distance == doctest::Approx(90.0f));
    CHECK(hit.normal.x == doctest::Approx(-1.0f));

    // Both of them, nearest first.
    eng::RaycastHit2D all[8];
    const int         count = eng::Physics2D::RaycastAll(
        eng::Vec2{0.0f, 0.0f}, eng::Vec2{1.0f, 0.0f}, 500.0f, all, 8);
    REQUIRE(count == 2);
    CHECK(all[0].distance < all[1].distance);
}

TEST_CASE("a raycast direction need not be normalized") {
    // Normalizing inside the query rather than expecting it of the caller.
    // Forgetting to is otherwise a silent change in what `distance` means.
    SceneFixture fixture;
    eng::Entity* wall = fixture.Add("Wall", eng::Vec2{100.0f, 0.0f});
    AddBox(*wall, eng::Vec2{10.0f, 10.0f});

    const eng::RaycastHit2D hit = eng::Physics2D::Raycast(
        eng::Vec2{0.0f, 0.0f}, eng::Vec2{57.0f, 0.0f}, 500.0f);
    REQUIRE(hit);
    CHECK(hit.distance == doctest::Approx(90.0f));
}

TEST_CASE("a query filter can skip triggers and ignore one entity") {
    SceneFixture fixture;

    eng::Entity* asker = fixture.Add("Asker", eng::Vec2{0.0f, 0.0f});
    AddBox(*asker, eng::Vec2{10.0f, 10.0f});

    eng::Entity* volume = fixture.Add("Volume", eng::Vec2{100.0f, 0.0f});
    AddBox(*volume, eng::Vec2{10.0f, 10.0f})->SetTrigger(true);

    // Without a filter the ray hits the asker's own collider first, which is
    // the classic surprise the ignore field exists for.
    const eng::RaycastHit2D unfiltered =
        eng::Physics2D::Raycast(eng::Vec2{0.0f, 0.0f}, eng::Vec2{1.0f, 0.0f}, 500.0f);
    REQUIRE(unfiltered);
    CHECK(unfiltered.entity == asker->Id());

    eng::QueryFilter skipSelf;
    skipSelf.ignore = asker->Id();
    const eng::RaycastHit2D trigger = eng::Physics2D::Raycast(
        eng::Vec2{0.0f, 0.0f}, eng::Vec2{1.0f, 0.0f}, 500.0f, skipSelf);
    REQUIRE(trigger);
    CHECK(trigger.entity == volume->Id());

    eng::QueryFilter skipBoth;
    skipBoth.ignore      = asker->Id();
    skipBoth.hitTriggers = false;
    CHECK(!eng::Physics2D::Raycast(eng::Vec2{0.0f, 0.0f}, eng::Vec2{1.0f, 0.0f}, 500.0f,
                                   skipBoth));
}

TEST_CASE("a query filter can name one layer") {
    SceneFixture fixture;

    eng::Entity* wall = fixture.Add("Wall", eng::Vec2{100.0f, 0.0f});
    AddBox(*wall, eng::Vec2{10.0f, 10.0f})->SetLayer("Wall");

    eng::Entity* water = fixture.Add("Water", eng::Vec2{50.0f, 0.0f});
    AddBox(*water, eng::Vec2{10.0f, 10.0f})->SetLayer("Water");

    eng::QueryFilter wallsOnly;
    wallsOnly.layer = "Wall";
    const eng::RaycastHit2D hit = eng::Physics2D::Raycast(
        eng::Vec2{0.0f, 0.0f}, eng::Vec2{1.0f, 0.0f}, 500.0f, wallsOnly);

    REQUIRE(hit);
    CHECK(hit.entity == wall->Id());   // the nearer Water was skipped
}

TEST_CASE("a circle cast stops a radius short of what a thin ray would hit") {
    // The exactness claim in Physics2D.h: sliding a circle of radius r into a
    // shape is the same as firing a thin ray at that shape grown by r. If this
    // ever drifts, a character-sized sweep starts clipping into walls.
    SceneFixture fixture;

    eng::Entity* wall = fixture.Add("Wall", eng::Vec2{100.0f, 0.0f});
    AddBox(*wall, eng::Vec2{10.0f, 10.0f});

    const eng::RaycastHit2D thin =
        eng::Physics2D::Raycast(eng::Vec2{0.0f, 0.0f}, eng::Vec2{1.0f, 0.0f}, 500.0f);
    const eng::RaycastHit2D fat = eng::Physics2D::CircleCast(
        eng::Vec2{0.0f, 0.0f}, 5.0f, eng::Vec2{1.0f, 0.0f}, 500.0f);

    REQUIRE(thin);
    REQUIRE(fat);
    CHECK(thin.distance == doctest::Approx(90.0f));
    CHECK(fat.distance == doctest::Approx(85.0f));
}

TEST_CASE("overlap queries find what is in range and nothing else") {
    SceneFixture fixture;

    eng::Entity* close = fixture.Add("Close", eng::Vec2{10.0f, 0.0f});
    AddBox(*close, eng::Vec2{5.0f, 5.0f});

    eng::Entity* alsoClose = fixture.Add("AlsoClose", eng::Vec2{0.0f, 10.0f});
    AddBox(*alsoClose, eng::Vec2{5.0f, 5.0f});

    eng::Entity* distant = fixture.Add("Distant", eng::Vec2{900.0f, 0.0f});
    AddBox(*distant, eng::Vec2{5.0f, 5.0f});

    eng::ColliderComponent* found[8];
    CHECK(eng::Physics2D::OverlapCircle(eng::Vec2{0.0f, 0.0f}, 30.0f, found, 8) == 2);
    CHECK(eng::Physics2D::OverlapCircle(eng::Vec2{0.0f, 0.0f}, 3.0f, found, 8) == 0);
    CHECK(eng::Physics2D::OverlapBox(eng::Vec2{0.0f, 0.0f}, eng::Vec2{60.0f, 60.0f},
                                     0.0f, found, 8) == 2);
}

TEST_CASE("ClosestPoint lands on the collider's surface") {
    SceneFixture fixture;
    eng::Entity* entity = fixture.Add("Box", eng::Vec2{0.0f, 0.0f});
    eng::BoxColliderComponent* box = AddBox(*entity, eng::Vec2{10.0f, 10.0f});

    const eng::Vec2 outside = box->ClosestPoint(eng::Vec2{100.0f, 3.0f});
    CHECK(outside.x == doctest::Approx(10.0f));
    CHECK(outside.y == doctest::Approx(3.0f));
}

TEST_CASE("Distance reports a gap when apart and an overlap when not") {
    SceneFixture fixture;

    eng::Entity* a = fixture.Add("A", eng::Vec2{0.0f, 0.0f});
    eng::BoxColliderComponent* boxA = AddBox(*a, eng::Vec2{10.0f, 10.0f});

    eng::Entity* b = fixture.Add("B", eng::Vec2{50.0f, 0.0f});
    eng::BoxColliderComponent* boxB = AddBox(*b, eng::Vec2{10.0f, 10.0f});

    const eng::Physics2D::Distance2D apart = eng::Physics2D::Distance(*boxA, *boxB);
    CHECK(!apart.overlapped);
    CHECK(apart.distance == doctest::Approx(30.0f));

    b->Transform().SetLocalPosition(eng::Vec2{15.0f, 0.0f});
    const eng::Physics2D::Distance2D touching = eng::Physics2D::Distance(*boxA, *boxB);
    CHECK(touching.overlapped);
    CHECK(touching.distance == doctest::Approx(-5.0f));
}

// ===========================================================================
//  A kinematic body against something that cannot move
//
//  Rigidbody.h recommends a kinematic body FOR A CHARACTER CONTROLLER, and a
//  character controller that walks through walls is not one. These are the
//  cases that said it did.
// ===========================================================================

TEST_CASE("a kinematic body is stopped by static scenery") {
    // ======================================================================
    //  THE REPORTED BUG, AS A TEST.
    //
    //  Both sides report an inverse mass of zero - "immovable" - so the sum
    //  was zero and both halves of the solver returned without doing
    //  anything. The body sailed through the wall at full speed.
    // ======================================================================
    SceneFixture fixture;
    eng::Physics2D::SetGravity(eng::Vec2{0.0f, 0.0f});   // walking, not falling

    // A wall at x = 100, two units thick.
    eng::Entity* wall = fixture.Add("Wall", eng::Vec2{100.0f, 0.0f});
    AddBox(*wall, eng::Vec2{2.0f, 100.0f});
    AddBody(*wall)->SetBodyType(eng::RigidbodyType::Static);

    eng::Entity*               entity = fixture.Add("Player", eng::Vec2{0.0f, 0.0f});
    AddBox(*entity, eng::Vec2{8.0f, 16.0f});
    eng::Rigidbody2DComponent* player = AddBody(*entity);
    player->SetBodyType(eng::RigidbodyType::Kinematic);
    player->SetFreezeRotation(true);

    // Walking right, hard, for two seconds. Unstopped it would reach x = 520.
    for (int i = 0; i < 120; ++i) {
        player->SetVelocity(eng::Vec2{260.0f, 0.0f});   // a script, every step
        Step();
    }

    // SHORT OF THE WALL'S NEAR FACE, give or take the sliver of overlap the
    // position correction deliberately leaves - see kSlop in Collider.cpp.
    const float x = entity->Transform().WorldPosition().x;
    CHECK(x < 95.0f);
    CHECK(x > 85.0f);   // and it DID walk up to it rather than stopping early
}

TEST_CASE("a kinematic body slides along a wall rather than sticking to it") {
    // The other half of what a character controller needs: the velocity INTO
    // the surface goes, the velocity ALONG it survives. A solver that zeroed
    // the whole vector would make walls flypaper.
    SceneFixture fixture;
    eng::Physics2D::SetGravity(eng::Vec2{0.0f, 0.0f});

    eng::Entity* wall = fixture.Add("Wall", eng::Vec2{100.0f, 0.0f});
    AddBox(*wall, eng::Vec2{2.0f, 400.0f});
    AddBody(*wall)->SetBodyType(eng::RigidbodyType::Static);

    eng::Entity* entity = fixture.Add("Player", eng::Vec2{80.0f, 0.0f});

    // FRICTIONLESS, so that this is about the solver removing the velocity
    // INTO the wall and nothing else. With grip, sliding up it would also be
    // slowed by friction and the test would be measuring two things.
    eng::BoxColliderComponent* box = AddBox(*entity, eng::Vec2{8.0f, 16.0f});
    eng::PhysicsMaterial2D     slippery;
    slippery.friction = 0.0f;
    box->SetMaterial(slippery);
    eng::Rigidbody2DComponent* player = AddBody(*entity);
    player->SetBodyType(eng::RigidbodyType::Kinematic);
    player->SetFreezeRotation(true);

    for (int i = 0; i < 60; ++i) {
        player->SetVelocity(eng::Vec2{260.0f, 120.0f});   // into it AND along it
        Step();
    }

    // It went UP, which is the whole point.
    CHECK(entity->Transform().WorldPosition().y > 80.0f);
    // And not THROUGH.
    CHECK(entity->Transform().WorldPosition().x < 95.0f);
}

TEST_CASE("a kinematic body still shoves a dynamic one and is not shoved back") {
    // ======================================================================
    //  THE THING THAT MUST NOT CHANGE.
    //
    //  Zero inverse mass against anything that CAN move is what makes a lift
    //  carry a crate and a door shove a barrel - "infinite mass, in effect",
    //  as Rigidbody.h puts it. The fix above only applies when the other side
    //  cannot move at all, and this is the case that proves it did not leak.
    // ======================================================================
    SceneFixture fixture;
    eng::Physics2D::SetGravity(eng::Vec2{0.0f, 0.0f});

    eng::Entity*               doorEntity = fixture.Add("Door", eng::Vec2{0.0f, 0.0f});
    AddBox(*doorEntity, eng::Vec2{8.0f, 16.0f});
    eng::Rigidbody2DComponent* door = AddBody(*doorEntity);
    door->SetBodyType(eng::RigidbodyType::Kinematic);
    door->SetFreezeRotation(true);

    eng::Entity*               barrelEntity = fixture.Add("Barrel", eng::Vec2{40.0f, 0.0f});
    AddBox(*barrelEntity, eng::Vec2{8.0f, 16.0f});
    eng::Rigidbody2DComponent* barrel = AddBody(*barrelEntity);
    barrel->SetBodyType(eng::RigidbodyType::Dynamic);
    barrel->SetFreezeRotation(true);

    for (int i = 0; i < 60; ++i) {
        door->SetVelocity(eng::Vec2{100.0f, 0.0f});
        Step();
    }

    // THE DOOR WENT WHERE IT WAS TOLD, undisturbed: 60 steps at 100 px/s.
    CHECK(doorEntity->Transform().WorldPosition().x == doctest::Approx(100.0f).epsilon(0.05));

    // AND THE BARREL WAS PUSHED AHEAD OF IT.
    CHECK(barrelEntity->Transform().WorldPosition().x > 100.0f);
}

TEST_CASE("two kinematic bodies are left alone, as Box2D leaves them") {
    // Each would see the other as immovable and both would give way, which is
    // a tug of war between two things a script is positioning. Documented in
    // SolverMasses rather than solved, so this pins the documented answer.
    SceneFixture fixture;
    eng::Physics2D::SetGravity(eng::Vec2{0.0f, 0.0f});

    eng::Entity*               leftEntity = fixture.Add("Left", eng::Vec2{0.0f, 0.0f});
    AddBox(*leftEntity, eng::Vec2{8.0f, 16.0f});
    eng::Rigidbody2DComponent* left = AddBody(*leftEntity);
    left->SetBodyType(eng::RigidbodyType::Kinematic);

    eng::Entity*               rightEntity = fixture.Add("Right", eng::Vec2{40.0f, 0.0f});
    AddBox(*rightEntity, eng::Vec2{8.0f, 16.0f});
    eng::Rigidbody2DComponent* right = AddBody(*rightEntity);
    right->SetBodyType(eng::RigidbodyType::Kinematic);

    for (int i = 0; i < 60; ++i) {
        left->SetVelocity(eng::Vec2{100.0f, 0.0f});
        Step();
    }

    // Straight through, both where their scripts put them.
    CHECK(leftEntity->Transform().WorldPosition().x == doctest::Approx(100.0f).epsilon(0.05));
    CHECK(rightEntity->Transform().WorldPosition().x == doctest::Approx(40.0f).epsilon(0.05));
}

TEST_CASE("a static body is never moved by a kinematic one walking into it") {
    // The asymmetry has a direction, and getting it the wrong way round would
    // push the scenery about instead.
    SceneFixture fixture;
    eng::Physics2D::SetGravity(eng::Vec2{0.0f, 0.0f});

    eng::Entity* wall = fixture.Add("Wall", eng::Vec2{100.0f, 0.0f});
    AddBox(*wall, eng::Vec2{2.0f, 100.0f});
    eng::Rigidbody2DComponent* wallBody = AddBody(*wall);
    wallBody->SetBodyType(eng::RigidbodyType::Static);

    eng::Entity*               entity = fixture.Add("Player", eng::Vec2{0.0f, 0.0f});
    AddBox(*entity, eng::Vec2{8.0f, 16.0f});
    eng::Rigidbody2DComponent* player = AddBody(*entity);
    player->SetBodyType(eng::RigidbodyType::Kinematic);

    for (int i = 0; i < 120; ++i) {
        player->SetVelocity(eng::Vec2{260.0f, 0.0f});
        Step();
    }

    CHECK(wall->Transform().WorldPosition().x == doctest::Approx(100.0f));
    CHECK(wallBody->Velocity().x == doctest::Approx(0.0f));
}

TEST_CASE("a kinematic body is stopped by a collider with no rigidbody at all") {
    // "No rigidbody" is the same thing as static - Rigidbody.h says so out
    // loud - so it has to behave the same way here. This is the commonest
    // shape of scenery in a scene somebody has just built.
    SceneFixture fixture;
    eng::Physics2D::SetGravity(eng::Vec2{0.0f, 0.0f});

    eng::Entity* wall = fixture.Add("Wall", eng::Vec2{100.0f, 0.0f});
    AddBox(*wall, eng::Vec2{2.0f, 100.0f});   // and NO body

    eng::Entity*               entity = fixture.Add("Player", eng::Vec2{0.0f, 0.0f});
    AddBox(*entity, eng::Vec2{8.0f, 16.0f});
    eng::Rigidbody2DComponent* player = AddBody(*entity);
    player->SetBodyType(eng::RigidbodyType::Kinematic);
    player->SetFreezeRotation(true);

    for (int i = 0; i < 120; ++i) {
        player->SetVelocity(eng::Vec2{260.0f, 0.0f});
        Step();
    }

    CHECK(entity->Transform().WorldPosition().x < 95.0f);
}

TEST_CASE("a kinematic body falls through nothing: gravity still ignores it") {
    // The fix must not have made a kinematic body dynamic by the back door.
    SceneFixture fixture;   // gravity is -1000 here

    eng::Entity*               entity = fixture.Add("Lift", eng::Vec2{0.0f, 0.0f});
    AddBox(*entity, eng::Vec2{8.0f, 16.0f});
    eng::Rigidbody2DComponent* lift = AddBody(*entity);
    lift->SetBodyType(eng::RigidbodyType::Kinematic);

    for (int i = 0; i < 60; ++i) {
        Step();
    }

    CHECK(entity->Transform().WorldPosition().y == doctest::Approx(0.0f));
    CHECK(lift->Velocity().y == doctest::Approx(0.0f));
}

// ===========================================================================
//  CONTACTS ARE KEYED BY COLLIDER PAIR, NOT BY ENTITY PAIR
//
//  Everything below is new coverage, and it was all unasserted: nothing in
//  this suite drove a contact as far as an EVENT, so which pair sent which
//  kind, and when, was checked by nothing at all. That bookkeeping is exactly
//  what the collider-pair key changed.
// ===========================================================================

#include <vector>

namespace {

// Removes ONE SPECIFIC collider. Entity::RemoveComponent takes a type NAME and
// so always takes the first of its kind, which is precisely the thing these
// cases must not do - every one of them has two box colliders on one entity.
bool RemoveCollider(eng::Entity& entity, eng::ColliderComponent* which) {
    for (std::size_t i = 0; i < entity.ComponentCount(); ++i) {
        if (entity.ComponentAt(i) == which) {
            return entity.RemoveComponentAt(i);
        }
    }
    return false;
}

// Counts the contact events the bus actually delivers to ONE entity.
//
// Subscribes to the same six message types the script system does, so this
// sees precisely what a script on that entity would be told - without needing
// a compiled script to hear it.
struct EventLog {
    int collisionEnter = 0;
    int collisionStay  = 0;
    int collisionExit  = 0;
    int triggerEnter   = 0;
    int triggerStay    = 0;
    int triggerExit    = 0;

    // Any event whose PARTNER is the receiver itself. Always a bug: it means
    // two colliders on one entity were treated as a pair.
    int namingItself = 0;

    explicit EventLog(eng::EntityId watch) : m_watch(watch) {
        // Anything a previous case queued and never delivered would otherwise
        // land in the first Deliver() below. Stale messages are dropped by
        // target validation anyway, but not relying on that keeps the counts
        // readable when one of these fails.
        eng::MessageBus::ClearQueue();

        Listen(eng::MessageTypes::kCollisionEnter, collisionEnter);
        Listen(eng::MessageTypes::kCollisionStay, collisionStay);
        Listen(eng::MessageTypes::kCollisionExit, collisionExit);
        Listen(eng::MessageTypes::kTriggerEnter, triggerEnter);
        Listen(eng::MessageTypes::kTriggerStay, triggerStay);
        Listen(eng::MessageTypes::kTriggerExit, triggerExit);
    }

    ~EventLog() {
        for (const eng::SubscriptionId id : m_subs) {
            eng::MessageBus::Unsubscribe(id);
        }
    }

    EventLog(const EventLog&)            = delete;
    EventLog& operator=(const EventLog&) = delete;

    int Total() const {
        return collisionEnter + collisionStay + collisionExit + triggerEnter +
               triggerStay + triggerExit;
    }

    void Forget() {
        collisionEnter = collisionStay = collisionExit = 0;
        triggerEnter = triggerStay = triggerExit = 0;
        namingItself = 0;
    }

private:
    void Listen(const char* type, int& counter) {
        m_subs.push_back(eng::MessageBus::SubscribeBroadcast(
            type, [this, &counter](const eng::Message& message) {
                if (message.target != m_watch) {
                    return;
                }
                ++counter;
                if (message.other == m_watch) {
                    ++namingItself;
                }
            }));
    }

    eng::EntityId                    m_watch;
    std::vector<eng::SubscriptionId> m_subs;
};

// One step, and then the delivery that normally happens at stage 500. Step()
// alone only QUEUES the events - see Messaging.h.
void Deliver(float dt = 1.0f / 60.0f) {
    Step(dt);
    eng::MessageBus::Dispatch();
}

// The arrangement Collider.h:227-229 recommends: a compound body, several
// colliders on the one entity that carries the rigidbody. Here one is solid
// and one is a trigger, and both overlap the mover.
//
// `triggerOffsetX` only moves the trigger box, which changes nothing about
// what overlaps what - it changes the order the broad phase sorts them in,
// which used to decide the outcome.
struct CompoundBody {
    eng::Entity* wall  = nullptr;
    eng::Entity* mover = nullptr;
    eng::ColliderComponent* solid   = nullptr;
    eng::ColliderComponent* sensor  = nullptr;

    CompoundBody(SceneFixture& fixture, float triggerOffsetX) {
        wall  = fixture.Add("Wall", eng::Vec2{0.0f, 0.0f});
        solid = AddBox(*wall, eng::Vec2{50.0f, 50.0f});

        sensor = AddBox(*wall, eng::Vec2{50.0f, 50.0f});
        sensor->SetOffset(eng::Vec2{triggerOffsetX, 0.0f});
        sensor->SetTrigger(true);

        mover = fixture.Add("Mover", eng::Vec2{40.0f, 0.0f});
        AddBox(*mover, eng::Vec2{20.0f, 20.0f});
        AddBody(*mover)->SetGravityScale(0.0f);
    }
};

} // namespace

TEST_CASE("a compound body solves its solid contact AND reports its trigger") {
    // =======================================================================
    //  THE HEADLINE CASE. Contacts used to be keyed by ENTITY pair while the
    //  broad phase tests COLLIDER pairs, and the store was a map::emplace,
    //  which does not overwrite - so one of these two contacts was silently
    //  dropped, and the survivor decided the fate of the whole entity pair:
    //
    //    the trigger one survived -> solid() is false, the solid contact is
    //                                never solved, and the mover walks
    //                                straight through a solid collider
    //    the solid one survived   -> the trigger never fires
    //
    //  Exactly one of the three checks below failed, depending on a sort.
    // =======================================================================
    SceneFixture fixture;
    CompoundBody scene(fixture, -1.0f);
    EventLog     heard(scene.wall->Id());

    const float startX = scene.mover->Transform().WorldPosition().x;
    Deliver();

    // Two collider pairs, not one entity pair.
    CHECK(eng::CollisionSystem::ActivePairCount() == 2);

    // The solid contact was solved...
    CHECK(scene.mover->Transform().WorldPosition().x > startX + 0.01f);

    // ...and BOTH kinds were reported, once each.
    CHECK(heard.collisionEnter == 1);
    CHECK(heard.triggerEnter == 1);
    CHECK(heard.Total() == 2);
}

TEST_CASE("and the sort order cannot change any of that") {
    // The same scene with the trigger box on the other side, so the broad
    // phase reaches the two collider pairs the other way round. Moving a
    // trigger box one pixel used to decide whether a SOLID collider blocked.
    SceneFixture fixture;
    CompoundBody scene(fixture, +1.0f);
    EventLog     heard(scene.wall->Id());

    const float startX = scene.mover->Transform().WorldPosition().x;
    Deliver();

    CHECK(eng::CollisionSystem::ActivePairCount() == 2);
    CHECK(scene.mover->Transform().WorldPosition().x > startX + 0.01f);
    CHECK(heard.collisionEnter == 1);
    CHECK(heard.triggerEnter == 1);
    CHECK(heard.Total() == 2);
}

TEST_CASE("two colliders on ONE entity do not collide with each other") {
    // =======================================================================
    //  Unity's rule, and without the guard a compound body overlaps ITSELF:
    //  the pair loop had no owner-equality test, so two colliders on one
    //  entity formed a real pair, stored a real contact, and the event loop
    //  sent it to both of its sides - which are the same entity. It was told
    //  it had touched itself, twice, every step.
    // =======================================================================
    SceneFixture fixture;

    eng::Entity* body = fixture.Add("Body", eng::Vec2{0.0f, 0.0f});
    AddBox(*body, eng::Vec2{50.0f, 50.0f});
    AddBox(*body, eng::Vec2{50.0f, 50.0f})->SetOffset(eng::Vec2{1.0f, 0.0f});

    EventLog heard(body->Id());
    Deliver();

    CHECK(eng::CollisionSystem::ActivePairCount() == 0);
    CHECK(heard.Total() == 0);
    CHECK(heard.namingItself == 0);
}

TEST_CASE("an overlap that ENDS sends exactly one exit") {
    // Nothing in the repository defined an OnCollisionExit or an OnTriggerExit
    // before, so the separation-driven exit - the ordinary one, not the
    // destroy-driven one - was delivered by code that no test had ever run.
    SceneFixture fixture;

    eng::Entity* a = fixture.Add("A", eng::Vec2{0.0f, 0.0f});
    AddBox(*a, eng::Vec2{20.0f, 20.0f});
    eng::Entity* b = fixture.Add("B", eng::Vec2{10.0f, 0.0f});
    AddBox(*b, eng::Vec2{20.0f, 20.0f});

    EventLog heard(a->Id());
    Deliver();
    REQUIRE(heard.collisionEnter == 1);
    heard.Forget();

    b->Transform().SetLocalPosition(eng::Vec2{900.0f, 0.0f});
    Deliver();

    CHECK(eng::CollisionSystem::ActivePairCount() == 0);
    CHECK(heard.collisionExit == 1);
    CHECK(heard.collisionStay == 0);

    // And it does not repeat on the following step.
    heard.Forget();
    Deliver();
    CHECK(heard.Total() == 0);
}

TEST_CASE("an overlap that persists enters once and then stays") {
    // THE CANONICAL-AND-STABLE GUARANTEE for the new key. A pair key that was
    // not ordered the same way every step - or an id that moved when a
    // collider was registered or removed - would make the diff miss, and the
    // overlap would ENTER again on every single step. See the note on CPair.
    SceneFixture fixture;

    eng::Entity* a = fixture.Add("A", eng::Vec2{0.0f, 0.0f});
    AddBox(*a, eng::Vec2{20.0f, 20.0f});
    eng::Entity* b = fixture.Add("B", eng::Vec2{10.0f, 0.0f});
    AddBox(*b, eng::Vec2{20.0f, 20.0f});

    EventLog heard(a->Id());
    for (int step = 0; step < 20; ++step) {
        Deliver();
    }

    CHECK(heard.collisionEnter == 1);
    CHECK(heard.collisionStay == 19);
    CHECK(heard.collisionExit == 0);
}

TEST_CASE("detaching ONE collider of a compound body ends only ITS pair") {
    // =======================================================================
    //  Unregister used to match pairs by the detaching collider's OWNER, so
    //  removing one collider erased the pairs held through its SIBLINGS too -
    //  and told the script those overlaps had ended while they were still
    //  perfectly live.
    // =======================================================================
    SceneFixture fixture;

    eng::Entity*            wall = fixture.Add("Wall", eng::Vec2{0.0f, 0.0f});
    eng::ColliderComponent* first = AddBox(*wall, eng::Vec2{50.0f, 50.0f});
    AddBox(*wall, eng::Vec2{50.0f, 50.0f})->SetOffset(eng::Vec2{1.0f, 0.0f});

    eng::Entity* mover = fixture.Add("Mover", eng::Vec2{40.0f, 0.0f});
    AddBox(*mover, eng::Vec2{20.0f, 20.0f});

    EventLog heard(wall->Id());
    Deliver();
    REQUIRE(eng::CollisionSystem::ActivePairCount() == 2);
    REQUIRE(heard.collisionEnter == 2);   // one per collider pair
    heard.Forget();

    // Take one collider away. OnDetach runs immediately, so the exit for its
    // pair is queued there and delivered by the next Dispatch.
    REQUIRE(RemoveCollider(*wall, first));
    eng::MessageBus::Dispatch();

    CHECK(heard.collisionExit == 1);      // ITS pair, and only its pair
    CHECK(heard.triggerExit == 0);

    // The sibling's overlap is untouched and goes on reporting.
    heard.Forget();
    Deliver();
    CHECK(eng::CollisionSystem::ActivePairCount() == 1);
    CHECK(heard.collisionStay == 1);
    CHECK(heard.collisionExit == 0);      // no second, spurious exit
}

TEST_CASE("an exit carries no contact, even while a sibling is still touching") {
    // =======================================================================
    //  THE ONE THE COLLIDER-PAIR KEY PUT AT RISK. An exit pair has no entry
    //  in the contact table, which is what physics/Contact.h promises at the
    //  separation and relativeVelocity fields. But FindContact answers per
    //  ENTITY pair - so once one collider of a compound body stops touching
    //  while a SIBLING carries on, the lookup would come back holding the
    //  sibling's LIVE contact and hand it to the exit handler, every number
    //  in it plausible. ScriptComponent::DispatchCollision therefore does not
    //  ask at all on an exit.
    // =======================================================================
    SceneFixture fixture;

    eng::Entity*            wall  = fixture.Add("Wall", eng::Vec2{0.0f, 0.0f});
    eng::ColliderComponent* going = AddBox(*wall, eng::Vec2{50.0f, 50.0f});
    AddBox(*wall, eng::Vec2{50.0f, 50.0f})->SetOffset(eng::Vec2{1.0f, 0.0f});

    eng::Entity* mover = fixture.Add("Mover", eng::Vec2{40.0f, 0.0f});
    AddBox(*mover, eng::Vec2{20.0f, 20.0f});

    Deliver();
    REQUIRE(eng::CollisionSystem::ActivePairCount() == 2);

    // The sibling is still touching, so FindContact still answers for this
    // pair of ENTITIES - which is precisely the trap.
    REQUIRE(RemoveCollider(*wall, going));
    Deliver();

    REQUIRE(eng::CollisionSystem::ActivePairCount() == 1);
    const eng::Collision2D* still =
        eng::CollisionSystem::FindContact(wall->Id(), mover->Id());
    CHECK(still != nullptr);              // the sibling's contact IS live...
    CHECK(still->separation < 0.0f);      // ...and has real numbers in it
}

TEST_CASE("FindContact answers with a SOLID contact, never a trigger's") {
    // A collision hook is handed whatever this returns, so a trigger's record
    // must never be it. Before contacts were keyed per collider there was one
    // slot per entity pair and the trigger could win it.
    SceneFixture fixture;
    CompoundBody scene(fixture, -1.0f);

    Deliver();

    const eng::Collision2D* contact =
        eng::CollisionSystem::FindContact(scene.wall->Id(), scene.mover->Id());
    REQUIRE(contact != nullptr);
    REQUIRE(contact->thisCollider != nullptr);
    REQUIRE(contact->otherCollider != nullptr);
    CHECK_FALSE(contact->thisCollider->IsTrigger());
    CHECK_FALSE(contact->otherCollider->IsTrigger());

    // Take the solid one away and only the trigger pair is left, so there is
    // no solid contact to answer with at all.
    REQUIRE(RemoveCollider(*scene.wall, scene.solid));
    Deliver();
    CHECK(eng::CollisionSystem::FindContact(scene.wall->Id(), scene.mover->Id()) ==
          nullptr);
}

TEST_CASE("a collider turned into a trigger mid-overlap closes the old pairing") {
    // =======================================================================
    //  SetTrigger is public and nothing stops a script calling it from inside
    //  a handler. The pair would then simply start sending the other family's
    //  STAY - an OnTriggerStay for something that never sent an
    //  OnTriggerEnter, and an OnCollisionEnter that never gets its exit. So
    //  the old lifecycle is closed and the new one opens as a fresh enter.
    // =======================================================================
    SceneFixture fixture;

    eng::Entity*            a   = fixture.Add("A", eng::Vec2{0.0f, 0.0f});
    eng::ColliderComponent* box = AddBox(*a, eng::Vec2{20.0f, 20.0f});
    eng::Entity*            b   = fixture.Add("B", eng::Vec2{10.0f, 0.0f});
    AddBox(*b, eng::Vec2{20.0f, 20.0f});

    EventLog heard(a->Id());
    Deliver();
    REQUIRE(heard.collisionEnter == 1);
    heard.Forget();

    box->SetTrigger(true);
    Deliver();

    CHECK(heard.collisionExit == 1);   // the solid pairing is closed...
    CHECK(heard.triggerEnter == 1);    // ...and the trigger one opens
    CHECK(heard.triggerStay == 0);     // never a stay with no enter
    CHECK(heard.collisionStay == 0);
}

// ===========================================================================
//  THE COLLIDER OUTLINES.
//
//  These were drawn from the end of CollisionSystem::Update, and an Update
//  only happens when the clock asks for a fixed step. A PAUSED clock asks for
//  none, and a paused clock is exactly what an editor sits in for the whole
//  time somebody is building a scene - so no collider was ever drawn while
//  editing, and a collider you had just added looked like it had not worked.
//
//  The drawing is now CollisionSystem::DrawGizmos, called by whoever wants to
//  see it. These cases are here because the bug was invisible to the suite
//  until Gizmos::QueuedCount existed to ask.
// ===========================================================================

TEST_CASE("colliders are drawn with no simulation step having run") {
    SceneFixture fixture;

    eng::Entity* entity = fixture.Add("Box", eng::Vec2{0.0f, 0.0f});
    AddBox(*entity, eng::Vec2{20.0f, 20.0f});

    eng::Gizmos::Clear();
    eng::Gizmos::SetCategoryEnabled(eng::GizmoCategory::Colliders, true);

    // A STEP, FIRST, AND IT MUST DRAW NOTHING. The simulation's job is to
    // simulate; if it also drew, a shipped game would draw its own collision
    // outlines over itself - which is what used to happen.
    eng::CollisionSystem collision;
    collision.Update(1.0f / 60.0f);
    CHECK(eng::Gizmos::QueuedCount() == 0);

    // And now the thing an editor calls, with no step before it at all.
    eng::CollisionSystem::DrawGizmos(false);
    const std::size_t oneBox = eng::Gizmos::QueuedCount();
    CHECK(oneBox > 0);

    // A SECOND COLLIDER IS MORE DRAWING, which is the cheap way of saying
    // "every collider, not just the first".
    eng::Entity* second = fixture.Add("Second", eng::Vec2{100.0f, 0.0f});
    AddBox(*second, eng::Vec2{20.0f, 20.0f});

    eng::Gizmos::Clear();
    eng::CollisionSystem::DrawGizmos(false);
    CHECK(eng::Gizmos::QueuedCount() > oneBox);

    // A DISABLED COLLIDER IS STILL DRAWN - greyed, not left out. "Where did
    // my collider go" is the question the grey outline answers.
    eng::FindCollider(*second)->SetEnabled(false);
    eng::Gizmos::Clear();
    eng::CollisionSystem::DrawGizmos(false);
    CHECK(eng::Gizmos::QueuedCount() > oneBox);

    // And the Scene view's Gizmos menu turns the whole lot off.
    eng::Gizmos::SetCategoryEnabled(eng::GizmoCategory::Colliders, false);
    eng::Gizmos::Clear();
    eng::CollisionSystem::DrawGizmos(false);
    CHECK(eng::Gizmos::QueuedCount() == 0);

    eng::Gizmos::SetCategoryEnabled(eng::GizmoCategory::Colliders, true);
    eng::Gizmos::Clear();
}

// ===========================================================================
//  WHICH COMPONENTS AN ENTITY MAY HAVE SEVERAL OF.
//
//  A compound body is several colliders on one entity, and this file already
//  checks that one works: they do not collide with each other, and each one
//  reports its own events. What was missing was the EDITOR being able to build
//  one - its "Add component" list skipped any type the entity already had, so
//  a compound body was loadable from a scene file and not buildable by hand.
//
//  Component::AllowsSeveral is how the list now asks. These checks are the
//  whole of the engine side of it.
// ===========================================================================

TEST_CASE("a collider and a script may be had more than once; nothing else may") {
    SceneFixture fixture;
    eng::Entity* entity = fixture.Add("Thing", eng::Vec2{0.0f, 0.0f});

    const eng::Component* box = AddBox(*entity, eng::Vec2{10.0f, 10.0f});
    CHECK(box->AllowsSeveral());

    const eng::Component* body = AddBody(*entity);
    CHECK_FALSE(body->AllowsSeveral());

    const eng::Component* transform =
        entity->FindComponent(eng::TransformComponent::kTypeName);
    REQUIRE(transform != nullptr);
    CHECK_FALSE(transform->AllowsSeveral());

    const eng::Component* script =
        entity->AddComponent(eng::ScriptComponent::kTypeName);
    REQUIRE(script != nullptr);
    CHECK(script->AllowsSeveral());

    // AND THE SECOND ONE REALLY ATTACHES, which is the behaviour the flag is
    // claiming. Every shape of collider says yes, not only a box - the flag is
    // on the base class, so a sixth shape gets it without being edited.
    auto* second = static_cast<eng::CircleColliderComponent*>(
        entity->AddComponent(eng::CircleColliderComponent::kTypeName));
    REQUIRE(second != nullptr);
    second->SetRadius(20.0f);
    CHECK(second->AllowsSeveral());
    CHECK(eng::CollisionSystem::ColliderCount() == 2);
}

// ===========================================================================
//  The material, which is the pair of numbers a surface is made of.
// ===========================================================================

TEST_CASE("friction brings a slide to a stop sooner the higher it is") {
    // Flat ground rather than a ramp, so the only thing being measured is how
    // much sideways speed a contact takes away. On a ramp the answer is mixed
    // up with how much of gravity is along the slope.
    SceneFixture fixture;

    eng::Entity* ground = fixture.Add("Ground", eng::Vec2{0.0f, -20.0f});
    AddBox(*ground, eng::Vec2{2000.0f, 20.0f})->SetMaterial({0.6f, 0.0f});

    // STARTED APART, and that is not cosmetic. Two identical bodies created
    // at the same spot are a degenerate contact: they shove each other out of
    // the way, one of them leaves the ground, and a test of FRICTION quietly
    // becomes a test of nothing at all.
    const auto slide = [&](const char* name, float friction, float startX) {
        eng::Entity* box = fixture.Add(name, eng::Vec2{startX, 20.5f});
        AddBox(*box, eng::Vec2{20.0f, 20.0f})->SetMaterial({friction, 0.0f});
        eng::Rigidbody2DComponent* body = AddBody(*box);
        body->SetFreezeRotation(true);
        body->SetVelocity(eng::Vec2{400.0f, 0.0f});
        return box;
    };

    eng::Entity* slippery = slide("Slippery", 0.0f, -800.0f);
    eng::Entity* grippy   = slide("Grippy", 1.0f, 0.0f);

    for (int i = 0; i < 60; ++i) {
        Step();
    }

    // DISTANCE TRAVELLED, not final position - they started in different
    // places on purpose.
    const float wentFar = slippery->Transform().WorldPosition().x + 800.0f;
    const float stopped = grippy->Transform().WorldPosition().x;

    CHECK(wentFar > stopped);

    // AND THE FRICTIONLESS ONE KEEPS ALL OF IT. A contact that bleeds speed
    // away with friction at zero is the bug this pins: it would make every
    // surface in a game slightly sticky, which reads as sluggish controls and
    // is very hard to trace back to here.
    const eng::Rigidbody2DComponent* body =
        slippery->Find<eng::Rigidbody2DComponent>();
    REQUIRE(body != nullptr);
    CHECK(body->Velocity().x == doctest::Approx(400.0f).epsilon(0.02));
}

TEST_CASE("a bouncier body returns higher, and bounciness of 1 adds nothing") {
    SceneFixture fixture;

    eng::Entity* ground = fixture.Add("Ground", eng::Vec2{0.0f, -20.0f});
    AddBox(*ground, eng::Vec2{2000.0f, 20.0f})->SetMaterial({0.3f, 0.0f});

    constexpr float kDropHeight = 300.0f;

    // APART, for the same reason as the friction case above: two balls
    // dropped down the same line start overlapping and fling each other.
    const auto drop = [&](const char* name, float bounciness, float x) {
        eng::Entity* ball = fixture.Add(name, eng::Vec2{x, kDropHeight});
        auto* collider = static_cast<eng::CircleColliderComponent*>(
            ball->AddComponent(eng::CircleColliderComponent::kTypeName));
        collider->SetRadius(20.0f);
        collider->SetMaterial({0.3f, bounciness});
        AddBody(*ball)->SetFreezeRotation(true);
        return ball;
    };

    eng::Entity* dead   = drop("Dead", 0.0f, -200.0f);
    eng::Entity* lively = drop("Lively", 1.0f, 200.0f);

    // Long enough to fall, bounce, and come back up. The highest point each
    // one reaches AFTER its first contact is what is being compared, so it is
    // recorded as the simulation runs rather than sampled at the end.
    float deadPeak   = 0.0f;
    float livelyPeak = 0.0f;
    bool  landed     = false;

    for (int i = 0; i < 180; ++i) {
        Step();
        if (!landed) {
            landed = dead->Transform().WorldPosition().y < 30.0f;
            continue;
        }
        deadPeak   = std::max(deadPeak, dead->Transform().WorldPosition().y);
        livelyPeak = std::max(livelyPeak, lively->Transform().WorldPosition().y);
    }

    CHECK(livelyPeak > deadPeak + 50.0f);

    // NOT HIGHER THAN IT STARTED. Bounciness above 1 would add energy every
    // contact and the ball would climb out of the level; the clamp on the way
    // in is what stops that, and 1 exactly is the edge of it.
    CHECK(livelyPeak <= kDropHeight + 20.0f);
}

// ===========================================================================
//  Rotation, which a box collider only gained when it stopped being an AABB.
// ===========================================================================

TEST_CASE("a push away from the centre of mass spins the body") {
    SceneFixture fixture;

    eng::Entity* box = fixture.Add("Box", eng::Vec2{0.0f, 0.0f});
    AddBox(*box, eng::Vec2{10.0f, 60.0f});
    eng::Rigidbody2DComponent* body = AddBody(*box);

    REQUIRE(body->AngularVelocity() == doctest::Approx(0.0f));

    // Pushed sideways at the TOP of a tall box. The lever arm is 60 units, so
    // this has to produce spin as well as sideways motion - a solver that
    // dropped the angular term would move it and leave it upright, and a
    // tower that slides instead of toppling is the symptom.
    body->AddForceAtPosition(eng::Vec2{200.0f, 0.0f}, eng::Vec2{0.0f, 60.0f},
                             eng::ForceMode::Impulse);

    CHECK(body->Velocity().x > 0.0f);
    CHECK(body->AngularVelocity() != doctest::Approx(0.0f));

    // CLOCKWISE, which is negative. A push to the right applied above the
    // centre tips the top to the right, so the body turns clockwise - and a
    // sign error here makes everything in a game topple the wrong way.
    CHECK(body->AngularVelocity() < 0.0f);

    // AND FREEZING ROTATION REALLY FREEZES IT, which is what a character
    // controller relies on so that a capsule does not lie down.
    eng::Entity* frozen = fixture.Add("Frozen", eng::Vec2{200.0f, 0.0f});
    AddBox(*frozen, eng::Vec2{10.0f, 60.0f});
    eng::Rigidbody2DComponent* locked = AddBody(*frozen);
    locked->SetFreezeRotation(true);
    locked->AddForceAtPosition(eng::Vec2{200.0f, 0.0f}, eng::Vec2{200.0f, 60.0f},
                               eng::ForceMode::Impulse);
    CHECK(locked->Velocity().x > 0.0f);
    CHECK(locked->AngularVelocity() == doctest::Approx(0.0f));
}

// ===========================================================================
//  TUNNELLING, which is a PROPERTY OF DISCRETE COLLISION and not a defect.
//
//  Every step moves a body first and looks for overlaps afterwards - see
//  SystemOrder.h. So a body that travels further in one step than the wall is
//  thick can be on one side at the end of one step and the far side at the end
//  of the next, with no step in between where the two overlapped.
//
//  IT IS PINNED DOWN HERE SO IT IS A DECISION RATHER THAN A SURPRISE. A game
//  that fires fast things sweeps a CircleCast ahead of them instead - which is
//  exactly why Physics2D has one.
// ===========================================================================

TEST_CASE("a slow body is stopped by a thin wall and a fast one goes through") {
    SceneFixture fixture;

    // Six units thick. At a sixtieth of a second a body doing 120 units a
    // second moves two units a step and cannot miss it; one doing 2400 moves
    // forty and cannot see it.
    eng::Entity* wall = fixture.Add("Wall", eng::Vec2{0.0f, 0.0f});
    AddBox(*wall, eng::Vec2{3.0f, 200.0f});

    const auto fire = [&](const char* name, float speed) {
        eng::Entity* bullet = fixture.Add(name, eng::Vec2{-100.0f, 0.0f});
        auto* collider = static_cast<eng::CircleColliderComponent*>(
            bullet->AddComponent(eng::CircleColliderComponent::kTypeName));
        collider->SetRadius(2.0f);
        eng::Rigidbody2DComponent* body = AddBody(*bullet);
        body->SetGravityScale(0.0f);
        body->SetFreezeRotation(true);
        body->SetVelocity(eng::Vec2{speed, 0.0f});
        return bullet;
    };

    eng::Entity* slow = fire("Slow", 120.0f);
    eng::Entity* fast = fire("Fast", 2400.0f);

    for (int i = 0; i < 120; ++i) {
        Step();
    }

    // The slow one is held on the near side of the wall.
    CHECK(slow->Transform().WorldPosition().x < 0.0f);

    // The fast one is long past it, with nothing reported on the way.
    CHECK(fast->Transform().WorldPosition().x > 100.0f);
}


// ===========================================================================
//  THE RESTING CONTACT, which is the thing everything else stands on.
//
//  Every case below existed as a measurement first. The numbers in the
//  comments are what the engine actually did before the fix beside them, and
//  they are there because "a box resting on the floor" is the single most
//  common thing in a 2D game and all of it was wrong.
// ===========================================================================

TEST_CASE("the contact point is the middle of the shared region, not a far corner") {
    // =======================================================================
    //  Collide's point was the average of the two SUPPORT VERTICES along the
    //  normal, and Support breaks a tie by keeping the first maximum. For a
    //  narrow box on a wide floor that is the floor's FAR corner and the
    //  box's NEAR corner, so the point landed (widthA - widthB) / 2 away from
    //  the contact. Measured: x = 190 for a 40-wide box on an 800-wide floor,
    //  which is outside the box altogether.
    //
    //  Two EQUAL-width boxes cancel exactly, which is why it was never
    //  noticed and why a hand-built pair of identical boxes looks perfect.
    // =======================================================================
    const ConvexShape ground = ToConvex(
        AABB::FromCenterHalfExtents(Vec2{0.0f, -40.0f}, Vec2{400.0f, 40.0f}));
    const ConvexShape box = ToConvex(
        AABB::FromCenterHalfExtents(Vec2{0.0f, 19.9f}, Vec2{20.0f, 20.0f}));

    const Contact c = Collide(ground, box);
    REQUIRE(c.touching);
    CHECK(c.normal.y == doctest::Approx(1.0f));

    // CENTRED, and that is the whole fix. A lever arm here is a torque on a
    // body that is doing nothing but resting.
    CHECK(c.point.x == doctest::Approx(0.0f));

    // And the equal-width case it used to get right still is.
    const ConvexShape lower =
        ToConvex(AABB::FromCenterHalfExtents(Vec2{0.0f, 0.0f}, Vec2{20.0f, 20.0f}));
    const ConvexShape upper =
        ToConvex(AABB::FromCenterHalfExtents(Vec2{0.0f, 38.0f}, Vec2{20.0f, 20.0f}));
    CHECK(Collide(lower, upper).point.x == doctest::Approx(0.0f));

    // A circle is still exact: the point sits halfway between the surfaces.
    const Contact circles = Collide(ToConvex(Circle{Vec2{0.0f, 0.0f}, 1.0f}),
                                    ToConvex(Circle{Vec2{1.5f, 0.0f}, 1.0f}));
    REQUIRE(circles.touching);
    CHECK(circles.point.x == doctest::Approx(0.75f));
    CHECK(circles.point.y == doctest::Approx(0.0f));
}

TEST_CASE("a box resting on flat ground does not start turning") {
    // =======================================================================
    //  THE SYMPTOM THAT STARTED ALL OF THIS, and it happened with the DEFAULT
    //  settings: freezeRotation is false unless somebody sets it.
    //
    //  Measured before the contact point was fixed: the box picked up spin on
    //  the very first step it touched down, reached 13.6 rad/s, turned five
    //  and a half times and ended up 3471 units BELOW the floor after four
    //  seconds. Every resting test in this file sets freezeRotation(true),
    //  which zeroes InverseInertia and hid it completely.
    // =======================================================================
    SceneFixture fixture;

    eng::Entity* ground = fixture.Add("Ground", eng::Vec2{0.0f, -40.0f});
    AddBox(*ground, eng::Vec2{400.0f, 40.0f})->SetMaterial({0.6f, 0.0f});

    eng::Entity* box = fixture.Add("Box", eng::Vec2{0.0f, 22.0f});
    AddBox(*box, eng::Vec2{20.0f, 20.0f})->SetMaterial({0.6f, 0.0f});
    eng::Rigidbody2DComponent* body = AddBody(*box);
    REQUIRE_FALSE(body->FreezeRotation());   // the default, and the point

    for (int step = 0; step < 240; ++step) {
        Step();
    }

    // Sitting on the floor, not through it, not on its corner, not moving.
    CHECK(box->Transform().WorldPosition().y == doctest::Approx(20.0f).epsilon(0.01));
    CHECK(std::fabs(box->Transform().WorldRotation()) < 0.001f);
    CHECK(std::fabs(body->AngularVelocity()) < 0.001f);
    CHECK(std::fabs(body->Velocity().y) < 1.0f);
}

TEST_CASE("a stack of six boxes settles near its full height") {
    // =======================================================================
    //  The position half of the solver was ONE sweep over STALE separations,
    //  so a chain of contacts could not be satisfied: clearing the overlap at
    //  the bottom of a tower moves that box up into the one above it, and
    //  only another pass notices.
    //
    //  Measured at the shipped four solver iterations, bottom box upward:
    //      before   13.07  39.75  68.44   99.61  133.59  170.54
    //      after    18.41  56.69  95.30  134.28  173.69  213.53
    //      ideal    20     60     100     140     180     220
    //
    //  What is left is the VELOCITY half not fully converging in four passes
    //  over six contacts, which is what config/engine.json's solverIterations
    //  is for and what its note already says. At 32 the top box reaches 218.
    // =======================================================================
    SceneFixture fixture;

    eng::Entity* ground = fixture.Add("Ground", eng::Vec2{0.0f, -40.0f});
    AddBox(*ground, eng::Vec2{400.0f, 40.0f})->SetMaterial({0.6f, 0.0f});

    constexpr int kBoxes = 6;
    eng::Entity*  stack[kBoxes] = {};
    for (int i = 0; i < kBoxes; ++i) {
        char name[16];
        std::snprintf(name, sizeof(name), "Box%d", i);
        stack[i] = fixture.Add(name,
                               eng::Vec2{0.0f, 20.0f + static_cast<float>(i) * 42.0f});
        AddBox(*stack[i], eng::Vec2{20.0f, 20.0f})->SetMaterial({0.6f, 0.0f});
        AddBody(*stack[i])->SetFreezeRotation(true);
    }

    for (int step = 0; step < 240; ++step) {
        Step();
    }

    // THE TOP BOX IS THE MEASURE OF THE WHOLE TOWER, because every overlap
    // below it subtracts from where it ends up. 205 is comfortably past the
    // 170.5 the old solver reached and comfortably short of the ideal 220.
    CHECK(stack[kBoxes - 1]->Transform().WorldPosition().y > 205.0f);

    // And no two neighbours are deeply inside each other. The old solver left
    // the bottom pair 13 units into one another out of 40.
    for (int i = 1; i < kBoxes; ++i) {
        const float gap = stack[i]->Transform().WorldPosition().y -
                          stack[i - 1]->Transform().WorldPosition().y;
        CHECK(gap > 36.0f);
        CHECK(gap <= 40.5f);
    }

    // The bottom box is on the floor rather than in it.
    CHECK(stack[0]->Transform().WorldPosition().y > 17.0f);
}

TEST_CASE("a compound body resting flat is not fired off what it landed on") {
    // =======================================================================
    //  Three colliders on one entity - the compound body Collider.h
    //  recommends - gave three contacts with the ground, all the same depth,
    //  and the single stale sweep moved the body the full 80% for each one.
    //  Total 2.4x its own penetration: it was lifted clear, fell back, and
    //  repeated forever.
    // =======================================================================
    SceneFixture fixture;

    eng::Entity* ground = fixture.Add("Ground", eng::Vec2{0.0f, -40.0f});
    AddBox(*ground, eng::Vec2{400.0f, 40.0f})->SetMaterial({0.6f, 0.0f});

    eng::Entity* body = fixture.Add("Compound", eng::Vec2{0.0f, 22.0f});
    for (int i = 0; i < 3; ++i) {
        eng::BoxColliderComponent* part = AddBox(*body, eng::Vec2{20.0f, 20.0f});
        part->SetOffset(eng::Vec2{static_cast<float>(i - 1) * 40.0f, 0.0f});
        part->SetMaterial({0.6f, 0.0f});
    }
    eng::Rigidbody2DComponent* rb = AddBody(*body);
    rb->SetFreezeRotation(true);

    REQUIRE(eng::CollisionSystem::ColliderCount() == 4);   // ground plus three

    for (int step = 0; step < 180; ++step) {
        Step();
    }

    // THREE CONTACTS, one per collider, every step - which is the behaviour
    // the events already promise and is why this is worth checking here.
    CHECK(eng::CollisionSystem::ActivePairCount() == 3);

    // Resting, and still resting: a body being bounced by its own corrections
    // shows up as a velocity that never settles.
    CHECK(body->Transform().WorldPosition().y == doctest::Approx(20.0f).epsilon(0.02));
    CHECK(std::fabs(rb->Velocity().y) < 2.0f);
}

TEST_CASE("a body with any bounciness still comes to rest") {
    // Without a restitution threshold a bouncy body buzzes against the floor
    // forever: gravity adds a sliver of closing speed each step and the
    // contact hands a fraction of it back, so "is it standing still?" never
    // becomes true.
    SceneFixture fixture;

    eng::Entity* ground = fixture.Add("Ground", eng::Vec2{0.0f, -40.0f});
    AddBox(*ground, eng::Vec2{400.0f, 40.0f})->SetMaterial({0.5f, 0.0f});

    eng::Entity* ball = fixture.Add("Ball", eng::Vec2{0.0f, 200.0f});
    auto* collider = static_cast<eng::CircleColliderComponent*>(
        ball->AddComponent(eng::CircleColliderComponent::kTypeName));
    collider->SetRadius(20.0f);
    collider->SetMaterial({0.5f, 0.6f});
    eng::Rigidbody2DComponent* body = AddBody(*ball);
    body->SetFreezeRotation(true);

    for (int step = 0; step < 900; ++step) {   // fifteen seconds
        Step();
    }

    CHECK(ball->Transform().WorldPosition().y == doctest::Approx(20.0f).epsilon(0.02));
    CHECK(std::fabs(body->Velocity().y) < 1.0f);
}

// ===========================================================================
//  The ray code, which had four separate defects in one function.
// ===========================================================================

TEST_CASE("a circle cast finds a box, not one of its edges") {
    // =======================================================================
    //  CircleCast slides a circle by GROWING THE TARGET'S radius and firing a
    //  thin ray - and growing the radius is exactly what sent a shape into
    //  the rounded branch, which tested only the segment
    //  points[0]..points[count - 1]. For a box that is ONE SIDE of it. So a
    //  circle cast at a wall could pass straight through it, and the same
    //  went for every polygon and every tilemap collider in the scene.
    // =======================================================================
    SceneFixture fixture;

    eng::Entity* wall = fixture.Add("Wall", eng::Vec2{200.0f, 0.0f});
    AddBox(*wall, eng::Vec2{20.0f, 100.0f});

    // Fired at the MIDDLE of the box's left face, which is the part the old
    // code could not see: the segment it tested ran corner 0 to corner 3.
    const eng::RaycastHit2D hit = eng::Physics2D::CircleCast(
        eng::Vec2{0.0f, 0.0f}, 10.0f, eng::Vec2{1.0f, 0.0f}, 500.0f);

    REQUIRE(hit);
    CHECK(hit.collider == eng::FindCollider(*wall));

    // It stops a radius short of the face: the face is at x = 180, so a
    // 10-radius circle's centre stops at 170.
    CHECK(hit.distance == doctest::Approx(170.0f).epsilon(0.01));

    // And a thin ray at the same target stops at the face itself, which is
    // the relationship CircleCast's own header promises.
    const eng::RaycastHit2D thin = eng::Physics2D::Raycast(
        eng::Vec2{0.0f, 0.0f}, eng::Vec2{1.0f, 0.0f}, 500.0f);
    REQUIRE(thin);
    CHECK(thin.distance == doctest::Approx(180.0f).epsilon(0.01));
}

TEST_CASE("a ray misses a bare segment past its ends") {
    // A 2-point radius-0 shape - an edge collider at its default edgeRadius -
    // was clipped as a polygon, and two opposite half-planes intersect in the
    // INFINITE LINE through the segment. Every ray crossing that line counted
    // as a hit, however far past either end.
    const Vec2        points[2] = {Vec2{-50.0f, 0.0f}, Vec2{50.0f, 0.0f}};
    const ConvexShape segment   = ToConvex(points, 2, 0.0f);

    RaycastHit hit;

    // Straight down through the middle: a real crossing.
    REQUIRE(Raycast(Ray{Vec2{0.0f, 40.0f}, Vec2{0.0f, -1.0f}, 100.0f}, segment, hit));
    CHECK(hit.distance == doctest::Approx(40.0f));
    CHECK(hit.normal.y == doctest::Approx(1.0f));   // faces the ray

    // Straight down 200 units past the end, still crossing the segment's
    // LINE. This is what used to report a hit.
    CHECK_FALSE(
        Raycast(Ray{Vec2{250.0f, 40.0f}, Vec2{0.0f, -1.0f}, 100.0f}, segment, hit));

    // Just inside the end, which must still hit.
    REQUIRE(Raycast(Ray{Vec2{49.0f, 40.0f}, Vec2{0.0f, -1.0f}, 100.0f}, segment, hit));

    // Along the line: parallel, and no surface to report.
    CHECK_FALSE(
        Raycast(Ray{Vec2{-200.0f, 0.0f}, Vec2{1.0f, 0.0f}, 400.0f}, segment, hit));
}

TEST_CASE("a cast that finishes inside something reports it") {
    // Both stepping loops advanced in whole steps and stopped once the next
    // one would pass the end, so the final partial step was never sampled -
    // and the end of the sweep is the one place a caller asking "can I move
    // there?" most needs tested.
    SceneFixture fixture;

    eng::Entity* wall = fixture.Add("Wall", eng::Vec2{100.0f, 0.0f});
    AddBox(*wall, eng::Vec2{30.0f, 100.0f});

    // A 20x20 box moved 63 units right ends with its right face at 73, well
    // inside the wall's left face at 70. 63 is deliberately not a multiple of
    // the step, which is a quarter of the smaller side: 5.
    const eng::RaycastHit2D hit = eng::Physics2D::BoxCast(
        eng::Vec2{0.0f, 0.0f}, eng::Vec2{20.0f, 20.0f}, 0.0f,
        eng::Vec2{1.0f, 0.0f}, 63.0f);

    REQUIRE(hit);
    CHECK(hit.collider == eng::FindCollider(*wall));
}

TEST_CASE("the reported gap between two shapes is the largest one, not the first") {
    // Collide stopped at the first axis that separated them, which is proof
    // enough for `touching` and wrong for the gap: the distance between two
    // shapes is the LARGEST separation over every axis.
    const ConvexShape a = ToConvex(
        AABB::FromCenterHalfExtents(Vec2{0.0f, 0.0f}, Vec2{10.0f, 10.0f}));

    // Far away in x, barely clear in y. The x gap is 80, the y gap is 1.
    const ConvexShape b = ToConvex(
        AABB::FromCenterHalfExtents(Vec2{100.0f, 21.0f}, Vec2{10.0f, 10.0f}));

    const Contact c = Collide(a, b);
    CHECK_FALSE(c.touching);

    // AND THE ANSWER IS BETTER THAN THE AXIS-ALIGNED GAP. The axis set
    // includes the vertex-to-vertex directions, and the largest separation
    // over all of them is the TRUE corner-to-corner distance:
    // sqrt(80^2 + 1^2) = 80.00625. Taking the first positive axis gave a
    // flat 80 at best, and whichever axis happened to be tested first at
    // worst.
    CHECK(c.separation == doctest::Approx(80.00625f).epsilon(0.0001));

    // Pointing very nearly along x, as a corner-to-corner direction over
    // that distance and that offset should.
    CHECK(c.normal.x == doctest::Approx(1.0f).epsilon(0.001));
    CHECK(c.normal.y == doctest::Approx(0.0125f).epsilon(0.05));
}

// ===========================================================================
//  The rigidbody.
// ===========================================================================

TEST_CASE("inertia does not change as the body turns") {
    // It was taken from the collider's WORLD BOUNDS - the upright box AROUND
    // the rotated shape - so a 100x100 box measured 100x100 at zero degrees
    // and 141x141 at forty-five. Its inertia doubled and halved once per
    // revolution, which is not a property any rigid body has.
    SceneFixture fixture;

    eng::Entity* box = fixture.Add("Box", eng::Vec2{0.0f, 0.0f});
    AddBox(*box, eng::Vec2{50.0f, 50.0f});
    eng::Rigidbody2DComponent* body = AddBody(*box);

    const float upright = body->InverseInertia();
    REQUIRE(upright > 0.0f);

    box->Transform().SetLocalRotation(eng::kPi * 0.25f);
    CHECK(body->InverseInertia() == doctest::Approx(upright));

    box->Transform().SetLocalRotation(eng::kPi * 0.5f);
    CHECK(body->InverseInertia() == doctest::Approx(upright));

    // It DOES follow the scale, which is a real change of size.
    box->Transform().SetLocalRotation(0.0f);
    box->Transform().SetLocalScale(eng::Vec2{2.0f, 2.0f});
    CHECK(body->InverseInertia() < upright);
}

TEST_CASE("a collider offset from its entity is harder to spin") {
    // A body turns about its TRANSFORM ORIGIN. A collider offset away from it
    // is correspondingly harder to swing round - the parallel axis theorem -
    // and that term was missing, so a character whose collider sat at its
    // feet span like one centred on it.
    SceneFixture fixture;

    eng::Entity* centred = fixture.Add("Centred", eng::Vec2{0.0f, 0.0f});
    AddBox(*centred, eng::Vec2{10.0f, 10.0f});
    const float plain = AddBody(*centred)->InverseInertia();

    eng::Entity*               offset = fixture.Add("Offset", eng::Vec2{200.0f, 0.0f});
    eng::BoxColliderComponent* far    = AddBox(*offset, eng::Vec2{10.0f, 10.0f});
    far->SetOffset(eng::Vec2{0.0f, -100.0f});
    const float lifted = AddBody(*offset)->InverseInertia();

    REQUIRE(plain > 0.0f);
    REQUIRE(lifted > 0.0f);
    CHECK(lifted < plain);   // harder to spin, so a SMALLER inverse
}

TEST_CASE("MovePosition moves a body once, not forever") {
    // The catch-up velocity has to survive into the collision stage so the
    // solver can shove what a platform is pushing - and has to be gone by the
    // next step. It was not, so ONE call moved the body at that speed
    // forever: a lift told to go up 200 units went up 200 units every step.
    SceneFixture fixture;

    eng::Entity* lift = fixture.Add("Lift", eng::Vec2{0.0f, 0.0f});
    AddBox(*lift, eng::Vec2{50.0f, 10.0f});
    eng::Rigidbody2DComponent* body = AddBody(*lift);
    body->SetBodyType(eng::RigidbodyType::Kinematic);

    body->MovePosition(eng::Vec2{0.0f, 200.0f});

    Step();
    CHECK(lift->Transform().WorldPosition().y == doctest::Approx(200.0f));

    // Ten more steps with nothing told to it at all.
    for (int step = 0; step < 10; ++step) {
        Step();
    }
    CHECK(lift->Transform().WorldPosition().y == doctest::Approx(200.0f));
    CHECK(body->Velocity().y == doctest::Approx(0.0f));

    // AND IT STILL MOVES WHEN ASKED AGAIN, every step, which is how a
    // platform is actually driven.
    for (int step = 0; step < 5; ++step) {
        body->MovePosition(lift->Transform().WorldPosition() + eng::Vec2{10.0f, 0.0f});
        Step();
    }
    CHECK(lift->Transform().WorldPosition().x == doctest::Approx(50.0f));

    // ======================================================================
    //  AND IT LANDS EXACTLY ON THE TARGET UNDER A SCALED, ROTATED PARENT,
    //  which it did not.
    //
    //  MovePosition takes a WORLD position: the velocity it sets is
    //  (target - WorldPosition()) / step, and the integration then moves by
    //  velocity * step - so the two cancel and the body arrives exactly,
    //  whatever is above it in the hierarchy. That only holds because the
    //  move is applied in world space. It used to be applied as a LOCAL
    //  translation, so under a parent scaled (2,2) the body overshot by the
    //  parent's scale and never reached where it was told to go.
    // ======================================================================
    eng::Entity* room = fixture.Add("Room", eng::Vec2{300.0f, 0.0f});
    room->Transform().SetLocalScale(eng::Vec2{2.0f, 2.0f});
    room->Transform().SetLocalRotation(0.5f);

    eng::Entity* inner = fixture.Add("Inner lift", eng::Vec2{0.0f, 0.0f});
    AddBox(*inner, eng::Vec2{50.0f, 10.0f});
    eng::Rigidbody2DComponent* innerBody = AddBody(*inner);
    innerBody->SetBodyType(eng::RigidbodyType::Kinematic);
    inner->Transform().SetParent(&room->Transform());

    const eng::Vec2 target{377.0f, -123.0f};
    innerBody->MovePosition(target);
    Step();

    CHECK(inner->Transform().WorldPosition().x ==
          doctest::Approx(target.x).epsilon(0.0001));
    CHECK(inner->Transform().WorldPosition().y ==
          doctest::Approx(target.y).epsilon(0.0001));
}

TEST_CASE("a body's spin survives being saved and loaded") {
    // Deserialize has always read "angularVelocity" and Serialize never wrote
    // it, so saving a scene destroyed every body's spin while keeping its
    // linear velocity - and in the editor, Play then Stop goes through the
    // same snapshot.
    SceneFixture fixture;

    eng::Entity* spinner = fixture.Add("Spinner", eng::Vec2{0.0f, 0.0f});
    AddBox(*spinner, eng::Vec2{10.0f, 10.0f});
    eng::Rigidbody2DComponent* body = AddBody(*spinner);
    body->SetVelocity(eng::Vec2{3.0f, 4.0f});
    body->SetAngularVelocity(2.5f);

    eng::Json saved;
    REQUIRE(body->Serialize(saved));

    eng::Entity* copy = fixture.Add("Copy", eng::Vec2{100.0f, 0.0f});
    AddBox(*copy, eng::Vec2{10.0f, 10.0f});
    eng::Rigidbody2DComponent* restored = AddBody(*copy);

    std::string error;
    REQUIRE(restored->Deserialize(saved, error));
    CHECK(restored->Velocity().x == doctest::Approx(3.0f));
    CHECK(restored->AngularVelocity() == doctest::Approx(2.5f));
}

// ===========================================================================
//  WHAT A COLLIDER IS ONCE ITS ENTITY HAS BEEN SCALED.
// ===========================================================================

TEST_CASE("a capsule stretched on one axis does not get fatter") {
    // =======================================================================
    //  The radius used to be worked out from the UNSCALED size and then
    //  multiplied by max(scale.x, scale.y). A character sprite stretched
    //  vertically - scale (1, 3), an ordinary thing to do - therefore got a
    //  capsule THREE TIMES TOO WIDE: 60 units across instead of 20, with 20
    //  units of solid collider hanging off each side, snagging on every
    //  doorway it walked past.
    //
    //  Collider.h states the max() rule for a CIRCLE, where a non-uniform
    //  scale genuinely has no answer without an ellipse test, and then offers
    //  the capsule as the fix for when that matters.
    // =======================================================================
    SceneFixture fixture;

    eng::Entity* entity = fixture.Add("Character", eng::Vec2{0.0f, 0.0f});
    auto* capsule = static_cast<eng::CapsuleColliderComponent*>(
        entity->AddComponent(eng::CapsuleColliderComponent::kTypeName));
    capsule->SetSize(eng::Vec2{20.0f, 60.0f});
    capsule->SetDirection(eng::CapsuleDirection::Vertical);

    entity->Transform().SetLocalScale(eng::Vec2{1.0f, 3.0f});

    // Unity's rule: scale the SIZE, then derive. (20,60) * (1,3) = (20,180),
    // so the radius is 10 and the whole thing is 20 wide and 180 tall.
    const eng::AABB bounds = capsule->WorldBounds();
    CHECK(bounds.Size().x == doctest::Approx(20.0f).epsilon(0.01));
    CHECK(bounds.Size().y == doctest::Approx(180.0f).epsilon(0.01));

    // The other way round, to show it is not a lucky special case.
    entity->Transform().SetLocalScale(eng::Vec2{4.0f, 1.0f});
    const eng::AABB wide = capsule->WorldBounds();
    CHECK(wide.Size().x == doctest::Approx(80.0f).epsilon(0.01));
    CHECK(wide.Size().y == doctest::Approx(80.0f).epsilon(0.01));   // a circle
}

TEST_CASE("a scaled-up collider is heavier, and the two mass properties agree") {
    // Area() was local-space for all six colliders, so auto-mass ignored the
    // entity's scale: a 64x64 crate weighed exactly what a 16x16 one of the
    // same material weighed, and the two bounced off each other as equals.
    // The giveaway was that the SAME collider's other mass property - its
    // inertia - has always read the scaled geometry.
    SceneFixture fixture;

    const auto crate = [&](const char* name, float scale) {
        eng::Entity* e = fixture.Add(name, eng::Vec2{0.0f, 0.0f});
        AddBox(*e, eng::Vec2{8.0f, 8.0f});
        e->Transform().SetLocalScale(eng::Vec2{scale, scale});
        eng::Rigidbody2DComponent* body = AddBody(*e);
        body->SetUseAutoMass(true);
        return body;
    };

    const eng::Rigidbody2DComponent* small = crate("Small", 1.0f);
    const eng::Rigidbody2DComponent* big   = crate("Big", 4.0f);

    CHECK(small->Mass() == doctest::Approx(256.0f));      // 16 x 16
    CHECK(big->Mass() == doctest::Approx(4096.0f));       // 64 x 64, 16x more

    // AND THEY AGREE WITH EACH OTHER. Mass goes as the area - the square of
    // the scale - and inertia as mass times the square of the size again, so
    // the inverse inertia drops by the fourth power of the scale.
    CHECK(big->InverseInertia() < small->InverseInertia());
}

TEST_CASE("auto mass sums each collider's own density, whatever order they are in") {
    // It used to sum the AREAS and multiply the total by whichever density the
    // LAST collider happened to carry, so a compound body weighed 1000 or 200
    // purely according to component order - a five-fold swing from nothing.
    SceneFixture fixture;

    const auto build = [&](const char* name, float firstDensity,
                           float secondDensity) {
        eng::Entity* e = fixture.Add(name, eng::Vec2{0.0f, 0.0f});
        AddBox(*e, eng::Vec2{5.0f, 5.0f})->SetDensity(firstDensity);
        AddBox(*e, eng::Vec2{5.0f, 5.0f})->SetDensity(secondDensity);
        eng::Rigidbody2DComponent* body = AddBody(*e);
        body->SetUseAutoMass(true);
        return body->Mass();
    };

    // 100 at density 1 plus 100 at density 5 is 600, either way round.
    const float oneThenFive = build("A", 1.0f, 5.0f);
    const float fiveThenOne = build("B", 5.0f, 1.0f);

    CHECK(oneThenFive == doctest::Approx(600.0f));
    CHECK(fiveThenOne == doctest::Approx(600.0f));
}

TEST_CASE("a disabled collider weighs nothing and does not decide the spin") {
    // Neither loop checked Enabled(), so an attack hitbox that is live for
    // three frames made its body permanently seven times harder to push - and
    // because the inertia loop stopped at the FIRST collider it found, a
    // disabled one attached first defined the body's whole rotational
    // response. "Switched off, a collider takes part in nothing" is what
    // Collider.h promises.
    SceneFixture fixture;

    eng::Entity* entity = fixture.Add("Attacker", eng::Vec2{0.0f, 0.0f});

    // The hitbox is attached FIRST and is the big one, which is what made it
    // win both loops.
    eng::BoxColliderComponent* hitbox = AddBox(*entity, eng::Vec2{10.0f, 15.0f});
    hitbox->SetEnabled(false);
    AddBox(*entity, eng::Vec2{5.0f, 5.0f});

    eng::Rigidbody2DComponent* body = AddBody(*entity);
    body->SetUseAutoMass(true);

    CHECK(body->Mass() == doctest::Approx(100.0f));   // was 700
    const float withHitboxOff = body->InverseInertia();

    // Switching it on changes both, which is the other half of the promise:
    // the number has to respond to the flag rather than ignoring it.
    hitbox->SetEnabled(true);
    CHECK(body->Mass() == doctest::Approx(700.0f));
    CHECK(body->InverseInertia() != doctest::Approx(withHitboxOff));
}

// ===========================================================================
//  A compound body's events, each with its own contact.
// ===========================================================================

TEST_CASE("each collider of a compound body is handed its own contact") {
    // =======================================================================
    //  Collider.h promises "EVENTS ARE PER COLLIDER PAIR ... so a script
    //  counting them needs the collider, not the entity, to tell them apart".
    //  The dispatcher re-derived the contact from an ENTITY-keyed index, which
    //  holds one contact per entity pair - the deepest - so a wall with two
    //  colliders sent two OnCollisionEnter calls whose contacts were
    //  byte-identical: `thisCollider` named the same collider both times, and
    //  the shallower pair's contact was unreachable from any API.
    //
    //  The event now carries a token naming its collider pair; see
    //  CollisionSystem::ContactForEvent.
    // =======================================================================
    SceneFixture fixture;

    eng::Entity*            wall = fixture.Add("Wall", eng::Vec2{0.0f, 0.0f});
    eng::ColliderComponent* near = AddBox(*wall, eng::Vec2{50.0f, 50.0f});
    eng::ColliderComponent* far  = AddBox(*wall, eng::Vec2{50.0f, 50.0f});
    far->SetOffset(eng::Vec2{1.0f, 0.0f});   // one unit deeper into the mover

    eng::Entity* mover = fixture.Add("Mover", eng::Vec2{40.0f, 0.0f});
    AddBox(*mover, eng::Vec2{20.0f, 20.0f});

    // Collected from inside the handler, which is where a token is valid -
    // stage 400 fills them and stage 500 delivers them, with nothing able to
    // run in between.
    std::vector<const eng::ColliderComponent*> mine;
    std::vector<float>                         separations;

    // ANYTHING A PREVIOUS CASE QUEUED AND NEVER DELIVERED would land in the
    // Deliver() below carrying a token from a step that no longer exists -
    // and entity ids are reused between fixtures, so it can even pass the
    // target filter. EventLog clears the queue in its constructor for exactly
    // this reason.
    eng::MessageBus::ClearQueue();

    const eng::SubscriptionId sub = eng::MessageBus::SubscribeBroadcast(
        eng::MessageTypes::kCollisionEnter,
        [&](const eng::Message& message) {
            if (message.target != wall->Id()) {
                return;
            }
            const eng::Collision2D* contact =
                eng::CollisionSystem::ContactForEvent(message.i0);
            if (contact == nullptr) {
                return;   // no token: not one of ours
            }

            // Oriented for the RECEIVER, the way the dispatcher orients it.
            const bool flipped = contact->other != mover->Id();
            mine.push_back(flipped ? contact->otherCollider : contact->thisCollider);
            separations.push_back(contact->separation);
        });

    Deliver();
    eng::MessageBus::Unsubscribe(sub);

    // Two colliders, two events, TWO DIFFERENT contacts.
    REQUIRE(mine.size() == 2);
    CHECK(mine[0] != mine[1]);
    CHECK(((mine[0] == near && mine[1] == far) ||
           (mine[0] == far && mine[1] == near)));

    // And they really are different depths - one box reaches a unit further
    // in, so the two separations must differ by about that unit. They used to
    // be identical.
    REQUIRE(separations.size() == 2);
    CHECK(std::fabs(separations[0] - separations[1]) == doctest::Approx(1.0f).epsilon(0.05));
}

// ===========================================================================
//  The closest point, inside as well as out.
// ===========================================================================

TEST_CASE("the closest point from INSIDE a shape is still on its boundary") {
    // Collider.h: "For a point inside the collider, the nearest point on the
    // boundary". Two branches disagreed with that and with each other.

    // A CIRCLE used to return the query point unchanged, so a circle answered
    // this differently from a box of the same footprint.
    const ConvexShape circle = ToConvex(Circle{Vec2{0.0f, 0.0f}, 10.0f});
    const Vec2        inCircle = ClosestPoint(circle, Vec2{3.0f, 0.0f});
    CHECK(inCircle.x == doctest::Approx(10.0f));
    CHECK(inCircle.y == doctest::Approx(0.0f));

    // The box of the same footprint, which always got this right.
    const ConvexShape box =
        ToConvex(AABB::FromMinMax(Vec2{-10.0f, -10.0f}, Vec2{10.0f, 10.0f}));
    CHECK(ClosestPoint(box, Vec2{3.0f, 0.0f}).x == doctest::Approx(10.0f));

    // A ROUNDED BOX used to step from the core's surface TOWARD the query
    // point - which from inside is INWARD. Half-extents 10 with a radius of 5
    // has its flat surface at |y| = 15; asked about its own centre it
    // answered (0, -5): ten units inside that surface, and neither a boundary
    // point nor the query point.
    const Vec2        corners[4] = {Vec2{-10.0f, -10.0f}, Vec2{10.0f, -10.0f},
                                    Vec2{10.0f, 10.0f},   Vec2{-10.0f, 10.0f}};
    const ConvexShape rounded    = ToConvex(corners, 4, 5.0f);

    const Vec2 atCentre = ClosestPoint(rounded, Vec2{0.0f, 0.0f});
    CHECK(std::fabs(atCentre.y) == doctest::Approx(15.0f));

    // And a point inside the rounded SHELL - past the core, short of the
    // surface - lands on the surface too rather than on itself.
    const Vec2 inShell = ClosestPoint(rounded, Vec2{0.0f, -12.0f});
    CHECK(inShell.y == doctest::Approx(-15.0f));
    CHECK(inShell.x == doctest::Approx(0.0f));
}

// ===========================================================================
//  The queries.
// ===========================================================================

TEST_CASE("a circle cast reports where it touched, not where its centre stopped") {
    // `point` was taken from the GROWN shape the ray was fired at, whose
    // surface is one radius out in the world where nothing is. Unity reports
    // the surface point as `point` and the centre separately as `centroid`;
    // RaycastHit2D here has no centroid, so the surface point is the only
    // thing `point` can mean - and it is what it means for every other query.
    SceneFixture fixture;

    eng::Entity* ball = fixture.Add("Ball", eng::Vec2{100.0f, 0.0f});
    auto* collider = static_cast<eng::CircleColliderComponent*>(
        ball->AddComponent(eng::CircleColliderComponent::kTypeName));
    collider->SetRadius(10.0f);

    const eng::RaycastHit2D hit = eng::Physics2D::CircleCast(
        eng::Vec2{0.0f, 0.0f}, 5.0f, eng::Vec2{1.0f, 0.0f}, 500.0f);

    REQUIRE(hit);

    // The centre stops 15 out from the ball's centre: 85. That is `distance`,
    // which is what a caller moves the circle by.
    CHECK(hit.distance == doctest::Approx(85.0f).epsilon(0.001));

    // The point they TOUCHED is on the ball, at 90. It used to report 85.
    CHECK(hit.point.x == doctest::Approx(90.0f).epsilon(0.001));
    CHECK(hit.point.y == doctest::Approx(0.0f));
}

TEST_CASE("a circle cast against a box is exact, including at a corner") {
    // Physics2D.h promises CircleCast is "EXACT ... No stepping, no
    // approximation", which holds only if the ray code handles a radius on an
    // n-gon core. It used to collapse the core to one edge; then it stepped.
    // It now solves the real boundary - each edge pushed out by the radius,
    // plus an arc round each corner.
    SceneFixture fixture;

    eng::Entity* wall = fixture.Add("Wall", eng::Vec2{100.0f, 0.0f});
    AddBox(*wall, eng::Vec2{20.0f, 20.0f});   // x in [80,120], y in [-20,20]

    // Straight at the middle of the left face. The face is at x = 80 and the
    // circle has radius 10, so its centre stops at exactly 70.
    const eng::RaycastHit2D face = eng::Physics2D::CircleCast(
        eng::Vec2{0.0f, 0.0f}, 10.0f, eng::Vec2{1.0f, 0.0f}, 500.0f);
    REQUIRE(face);
    CHECK(face.distance == doctest::Approx(70.0f).epsilon(0.0001));
    CHECK(face.normal.x == doctest::Approx(-1.0f).epsilon(0.0001));

    // AT A CORNER, which is the part a flat-face-only solution cannot do. A
    // circle sliding along y = -30 toward +x meets the arc round the corner
    // (80, -20): the centre is 10 from that corner when it touches, and it is
    // already 10 below it, so it stops with its centre exactly at x = 80.
    const eng::RaycastHit2D corner = eng::Physics2D::CircleCast(
        eng::Vec2{0.0f, -30.0f}, 10.0f, eng::Vec2{1.0f, 0.0f}, 500.0f);
    REQUIRE(corner);
    CHECK(corner.distance == doctest::Approx(80.0f).epsilon(0.0001));

    // The normal there is RADIAL, pointing out of the corner - which is what
    // makes a sweep glance off a corner rather than stopping square on it.
    CHECK(corner.normal.y == doctest::Approx(-1.0f).epsilon(0.0001));

    // And a lane that clears the box entirely still misses.
    CHECK_FALSE(eng::Physics2D::CircleCast(eng::Vec2{0.0f, -45.0f}, 10.0f,
                                           eng::Vec2{1.0f, 0.0f}, 500.0f));
}

// ===========================================================================
//  World space.
// ===========================================================================

TEST_CASE("a body under a scaled parent falls at the rate its velocity says") {
    // A rigidbody's velocity is in WORLD units and was applied as a LOCAL
    // translation, so under a parent scaled (2,2) the body moved twice as far
    // as its velocity said: it accelerated through the world at twice gravity
    // while reporting the right number, and the contact solver - which works
    // entirely in world space - was resolving a body whose reported velocity
    // was not the one it had.
    SceneFixture fixture;

    eng::Entity* room = fixture.Add("Room", eng::Vec2{0.0f, 0.0f});
    room->Transform().SetLocalScale(eng::Vec2{2.0f, 2.0f});

    eng::Entity* plain = fixture.Add("Plain", eng::Vec2{0.0f, 0.0f});
    AddBox(*plain, eng::Vec2{5.0f, 5.0f});
    AddBody(*plain)->SetFreezeRotation(true);

    eng::Entity* child = fixture.Add("Child", eng::Vec2{200.0f, 0.0f});
    AddBox(*child, eng::Vec2{5.0f, 5.0f});
    AddBody(*child)->SetFreezeRotation(true);
    child->Transform().SetParent(&room->Transform());
    child->Transform().SetWorldPosition(eng::Vec2{200.0f, 0.0f});

    const float startPlain = plain->Transform().WorldPosition().y;
    const float startChild = child->Transform().WorldPosition().y;

    for (int step = 0; step < 30; ++step) {
        Step();
    }

    const float fellPlain = startPlain - plain->Transform().WorldPosition().y;
    const float fellChild = startChild - child->Transform().WorldPosition().y;

    REQUIRE(fellPlain > 1.0f);

    // THE SAME DISTANCE THROUGH THE WORLD. It used to be twice as far.
    CHECK(fellChild == doctest::Approx(fellPlain).epsilon(0.001));
}

// ===========================================================================
//  The defaults, which were Unity's metres in a pixel engine.
// ===========================================================================

TEST_CASE("a freshly added collider is big enough to see") {
    // "Add component -> BoxColliderComponent" on a 32x32 sprite used to give
    // a collider ONE PIXEL across - a thousandth of the sprite's area - which
    // also collapsed the entity's click box in the Scene view, because that
    // box comes from the collider when there is one. config/engine.json
    // already states the rule: this project works in PIXELS.
    SceneFixture fixture;
    eng::Entity* entity = fixture.Add("Fresh", eng::Vec2{0.0f, 0.0f});

    const auto spanOf = [&](const char* typeName) {
        eng::Entity* host = fixture.Add(typeName, eng::Vec2{0.0f, 0.0f});
        host->AddComponent(typeName);
        const eng::ColliderComponent* collider = eng::FindCollider(*host);
        REQUIRE(collider != nullptr);
        const eng::Vec2 size = collider->WorldBounds().Size();
        return std::max(size.x, size.y);
    };

    // Every one of the five, because the defaults were wrong in all of them.
    CHECK(spanOf(eng::BoxColliderComponent::kTypeName) >= 16.0f);
    CHECK(spanOf(eng::CircleColliderComponent::kTypeName) >= 16.0f);
    CHECK(spanOf(eng::CapsuleColliderComponent::kTypeName) >= 16.0f);
    CHECK(spanOf(eng::PolygonColliderComponent::kTypeName) >= 16.0f);
    CHECK(spanOf(eng::EdgeColliderComponent::kTypeName) >= 16.0f);

    // And the click box the Scene view builds from it is not a point.
    entity->AddComponent(eng::BoxColliderComponent::kTypeName);
    const eng::Vec2 clickable = eng::FindCollider(*entity)->WorldBounds().Size();
    CHECK(clickable.x > 1.0f);
    CHECK(clickable.y > 1.0f);
}

// ===========================================================================
//  WHAT AN ADVERSARIAL REVIEW FOUND IN THE FIXES THEMSELVES.
//
//  Thirteen defects, one of them a REGRESSION that 710 passing cases did not
//  notice because the case written alongside the fix exercised the one shape
//  the fix happened to be right for. These are the cases that would have
//  caught them.
// ===========================================================================

TEST_CASE("the closest point on a CAPSULE is on the side you asked from") {
    // =======================================================================
    //  THE REGRESSION, and the shape of it is worth remembering.
    //
    //  A 2-point core - every capsule, and every edge collider with a
    //  thickness - has TWO outward normals, because its two "edges" are the
    //  same segment walked both ways. They give an identical foot and an
    //  identical distance, and the loop that picks the nearest uses a strict
    //  <, so it keeps only the first. A fix that then consulted that single
    //  normal answered HALF THE PLANE ON THE WRONG SIDE.
    //
    //  Measured: a capsule 20 wide asked about a point 50 to its left
    //  answered 10 to its RIGHT - 60 units from the query instead of 40.
    //
    //  The case written with that fix used a 4-point core, where the kept
    //  normal really is the nearest edge's, so nothing caught it.
    // =======================================================================
    const ConvexShape capsule =
        ToConvex(Capsule{Vec2{0.0f, -20.0f}, Vec2{0.0f, 20.0f}, 10.0f});
    REQUIRE(capsule.count == 2);

    // 50 to the left: the answer is on the LEFT.
    const Vec2 left = ClosestPoint(capsule, Vec2{-50.0f, 0.0f});
    CHECK(left.x == doctest::Approx(-10.0f));
    CHECK(left.y == doctest::Approx(0.0f));

    // And to the right, which is the half that always worked.
    const Vec2 right = ClosestPoint(capsule, Vec2{50.0f, 0.0f});
    CHECK(right.x == doctest::Approx(10.0f));

    // PAST THE END CAP, where there is no edge interior to be nearest to and
    // the toward-the-query direction is the only sane answer. This returned
    // (10, 20) - round the side - at a distance of 31.6 instead of 20.
    const Vec2 above = ClosestPoint(capsule, Vec2{0.0f, 50.0f});
    CHECK(above.x == doctest::Approx(0.0f));
    CHECK(above.y == doctest::Approx(30.0f));

    // AND A THICK EDGE COLLIDER, which is the same shape and the case a
    // character standing on the ground actually hits: it was told the nearest
    // point was five pixels UNDER the ground.
    const ConvexShape ground =
        ToConvex(Capsule{Vec2{-100.0f, 0.0f}, Vec2{100.0f, 0.0f}, 5.0f});
    const Vec2 standing = ClosestPoint(ground, Vec2{0.0f, 20.0f});
    CHECK(standing.y == doctest::Approx(5.0f));
    CHECK(standing.x == doctest::Approx(0.0f));
}

TEST_CASE("the closest point directly above a rounded corner is on the top face") {
    // The gate that decides "is the query point inside the core" was strict,
    // and the dot product it tests is exactly ZERO on the half-line
    // extending a core edge past its vertex. So a query straight above a
    // corner fell through to the side face: 9.999 above the corner was right,
    // 10.000 was wrong, 10.001 was right again - a discontinuity rather than
    // a limit.
    const Vec2        corners[4] = {Vec2{-10.0f, -10.0f}, Vec2{10.0f, -10.0f},
                                    Vec2{10.0f, 10.0f},   Vec2{-10.0f, 10.0f}};
    const ConvexShape rounded    = ToConvex(corners, 4, 5.0f);

    // Exactly above the top-right corner. The top surface is at y = 15.
    const Vec2 above = ClosestPoint(rounded, Vec2{10.0f, 30.0f});
    CHECK(above.x == doctest::Approx(10.0f));
    CHECK(above.y == doctest::Approx(15.0f));

    // A point lying exactly ON the surface answers with itself.
    const Vec2 onIt = ClosestPoint(rounded, Vec2{10.0f, 15.0f});
    CHECK(onIt.x == doctest::Approx(10.0f));
    CHECK(onIt.y == doctest::Approx(15.0f));

    // And the neighbours either side, which bracketed the discontinuity.
    CHECK(ClosestPoint(rounded, Vec2{9.999f, 30.0f}).y == doctest::Approx(15.0f));
    CHECK(ClosestPoint(rounded, Vec2{10.001f, 30.0f}).y == doctest::Approx(15.0f));
}

TEST_CASE("a capsule under a sheared world matrix keeps its axis") {
    // =======================================================================
    //  A rotated child under a NON-UNIFORMLY SCALED parent composes to a
    //  matrix that is not scale-then-rotate-then-translate at all: it SHEARS,
    //  and GetScale() and GetRotation() cannot describe it between them.
    //
    //  Deriving the core axis from the EXTRACTED rotation therefore pointed
    //  it 37 degrees wrong and put both cap centres 32 px out, in a case the
    //  code it replaced had exactly right. The axis now comes from the matrix
    //  and only the LENGTH comes from the scaled size.
    // =======================================================================
    SceneFixture fixture;

    eng::Entity* parent = fixture.Add("Stretched", eng::Vec2{0.0f, 0.0f});
    parent->Transform().SetLocalScale(eng::Vec2{1.0f, 3.0f});

    eng::Entity* child = fixture.Add("Turned", eng::Vec2{0.0f, 0.0f});
    auto* capsule = static_cast<eng::CapsuleColliderComponent*>(
        child->AddComponent(eng::CapsuleColliderComponent::kTypeName));
    capsule->SetSize(eng::Vec2{32.0f, 64.0f});
    capsule->SetDirection(eng::CapsuleDirection::Vertical);
    child->Transform().SetParent(&parent->Transform());
    child->Transform().SetLocalRotation(eng::kPi * 0.25f);

    std::vector<eng::ConvexShape> shapes;
    REQUIRE(eng::GatherWorldShapes(*capsule, shapes) == 1);
    REQUIRE(shapes[0].count == 2);

    // The composed scale is (sqrt(5), sqrt(5)) = 2.23607, so the scaled size
    // is (71.554, 143.108): radius 35.777 and a core half-length of 35.777.
    // The core axis is the local (0,1) through the matrix, normalized:
    // (-0.31623, 0.94868). So the ends sit at -/+ (11.314, 33.941).
    CHECK(shapes[0].radius == doctest::Approx(35.777f).epsilon(0.001));
    CHECK(shapes[0].points[0].x == doctest::Approx(11.314f).epsilon(0.001));
    CHECK(shapes[0].points[0].y == doctest::Approx(-33.941f).epsilon(0.001));
    CHECK(shapes[0].points[1].x == doctest::Approx(-11.314f).epsilon(0.001));
    CHECK(shapes[0].points[1].y == doctest::Approx(33.941f).epsilon(0.001));
}

TEST_CASE("a rounded box weighs what its rounding makes it") {
    // Area() promises to be "the area of what WorldShapes() produces", and
    // WorldShapes hands the solver the core inflated by edgeRadius. A 32x64
    // box with a radius of 16 is 3876 units of shape and was reported as
    // 1024, so an auto-mass body was nearly four times too light for the
    // thing being pushed - while LocalSize() beside it already counted the
    // same rounding for the inertia.
    SceneFixture fixture;

    eng::Entity*               entity = fixture.Add("Rounded", eng::Vec2{0.0f, 0.0f});
    eng::BoxColliderComponent* box    = AddBox(*entity, eng::Vec2{16.0f, 32.0f});
    box->SetEdgeRadius(16.0f);

    // core 32 x 64 = 2048, plus perimeter 192 x 16 = 3072, plus a disc of
    // radius 16 = 804.25.
    CHECK(box->Area() == doctest::Approx(2048.0f + 3072.0f + 804.25f).epsilon(0.001));

    // With no rounding it is just the core, which is what it always was.
    box->SetEdgeRadius(0.0f);
    CHECK(box->Area() == doctest::Approx(2048.0f));
}

TEST_CASE("a body under a parent scaled flat still moves, and says nothing") {
    // =======================================================================
    //  A parent at scale (1, 0) - "usually one frame of an animation", as
    //  Transform2D.h puts it - cannot be inverted. Routing every body's
    //  motion through SetWorldPosition asked anyway, so Mat3::Inverse handed
    //  back the identity and warned: the body teleported to the parent's
    //  origin and wrote a line to the log once per body per step.
    //
    //  There is no local delta that produces the requested world one when the
    //  parent squashes the world onto a line, so the delta is applied
    //  locally - which is what the code before world-space motion did.
    // =======================================================================
    SceneFixture fixture;

    eng::Entity* flat = fixture.Add("Flat", eng::Vec2{500.0f, 300.0f});
    flat->Transform().SetLocalScale(eng::Vec2{1.0f, 0.0f});

    eng::Entity* child = fixture.Add("Child", eng::Vec2{0.0f, 50.0f});
    AddBox(*child, eng::Vec2{5.0f, 5.0f});
    AddBody(*child)->SetFreezeRotation(true);
    child->Transform().SetParent(&flat->Transform());
    child->Transform().SetLocalPosition(eng::Vec2{0.0f, 50.0f});

    for (int step = 0; step < 30; ++step) {
        Step();
    }

    // IT FELL, in its own space. It used to be pinned at the parent's origin.
    CHECK(child->Transform().LocalPosition().y < 49.0f);
    CHECK(child->Transform().LocalPosition().x == doctest::Approx(0.0f));
}

TEST_CASE("a script can take over from MovePosition without losing a step") {
    // The wipe that stops one MovePosition moving a body forever used to fire
    // unconditionally, so a script that called MovePosition once and then
    // switched to SetVelocity lost exactly one step - silently, with
    // Velocity() reading zero for the rest of that frame.
    SceneFixture fixture;

    eng::Entity* lift = fixture.Add("Lift", eng::Vec2{0.0f, 0.0f});
    AddBox(*lift, eng::Vec2{50.0f, 10.0f});
    eng::Rigidbody2DComponent* body = AddBody(*lift);
    body->SetBodyType(eng::RigidbodyType::Kinematic);

    body->MovePosition(eng::Vec2{0.0f, 200.0f});
    Step();
    REQUIRE(lift->Transform().WorldPosition().y == doctest::Approx(200.0f));

    // The step after: velocity control, and no MovePosition. 60 units a
    // second at a sixtieth of a second is one unit.
    body->SetVelocity(eng::Vec2{0.0f, 60.0f});
    Step();
    CHECK(lift->Transform().WorldPosition().y == doctest::Approx(201.0f));
    CHECK(body->Velocity().y == doctest::Approx(60.0f));

    // And letting go still stops it, which is the behaviour the wipe exists
    // for: the catch-up velocity from a target must not outlive its step.
    body->MovePosition(eng::Vec2{0.0f, 300.0f});
    Step();
    REQUIRE(lift->Transform().WorldPosition().y == doctest::Approx(300.0f));
    Step();
    CHECK(lift->Transform().WorldPosition().y == doctest::Approx(300.0f));
}

TEST_CASE("a corner hit reports a radial normal however small the radius") {
    // The corner-arc normal compared a SQUARED length against kTinyLength
    // itself, so any radius below about a thousandth of a unit fell back to
    // -ray.direction: a glancing hit on a corner arc reported a head-on
    // normal, ninety degrees out, which a script sliding or reflecting on it
    // would follow.
    //  NEAR THE ORIGIN AND NOT TANGENT, both deliberately. At x = 80 a
    //  radius of 0.0005 is below float32's resolution of 6400, and a ray
    //  aimed exactly ALONG the arc's lowest point is the one direction where
    //  "radial" is genuinely ambiguous - so that arrangement measures
    //  floating point, not this fix.
    const Vec2        corners[4] = {Vec2{-2.0f, -2.0f}, Vec2{2.0f, -2.0f},
                                    Vec2{2.0f, 2.0f},   Vec2{-2.0f, 2.0f}};
    const ConvexShape rounded    = ToConvex(corners, 4, 0.0008f);

    // STARTED OUTSIDE IN X, which matters: a ray whose origin already lies
    // between the core and the rounded surface is INSIDE the shape, and the
    // branch rightly reports that as distance 0 with the normal facing back.
    // From off to the left, the first piece of boundary it meets is the arc
    // round the core corner (-2, -2).
    RaycastHit hit;
    REQUIRE(
        Raycast(Ray{Vec2{-2.5f, -2.0005f}, Vec2{1.0f, 0.0f}, 10.0f}, rounded, hit));

    // RADIAL, which here means it has a real downward component. The old code
    // returned -ray.direction = (-1, 0) exactly, so normal.y was ZERO for
    // every radius below about a thousandth of a unit.
    CHECK(std::fabs(hit.normal.y) > 0.3f);
    CHECK(hit.normal.y < 0.0f);
    CHECK(hit.normal.x < 0.0f);

    // And a radius comfortably above the old threshold was always fine, which
    // is what made this hard to notice.
    const ConvexShape big = ToConvex(corners, 4, 0.5f);
    RaycastHit        bigHit;
    REQUIRE(Raycast(Ray{Vec2{-4.0f, -2.3f}, Vec2{1.0f, 0.0f}, 10.0f}, big, bigHit));
    CHECK(std::fabs(bigHit.normal.y) > 0.3f);

    // The arc round (-2, -2) with a radius of 0.5, met 0.3 below its centre:
    // the crossing is at x = -2.4 and the normal is (-0.8, -0.6).
    CHECK(bigHit.normal.x == doctest::Approx(-0.8f).epsilon(0.01));
    CHECK(bigHit.normal.y == doctest::Approx(-0.6f).epsilon(0.01));
}
