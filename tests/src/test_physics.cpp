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
        eng::Entity* entity =
            fixture.Add("Spread", eng::Vec2{static_cast<float>(i) * 500.0f, 0.0f});
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
