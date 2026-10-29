#pragma once

// ============================================================================
//  Collider.h - the shapes entities collide with, and the system that checks
//  them.
//
//  NO COLLISION MATHS IS WRITTEN IN THIS FILE. math/Overlap.h answers "are
//  these two shapes touching, which way do I push, and how deep?" for every
//  shape here. All this file does is wrap those answers in components, layers,
//  triggers and events. Keeping the maths as plain functions over there is
//  what makes that possible.
//
//  ==========================================================================
//  EVERY COLLIDER IS A ConvexShape IN THE END
//
//  A collider's job is to turn its own description - a size, a radius, a list
//  of points - plus its entity's world transform, into one or more
//  ConvexShapes in world space. That is WorldShapes(), and it is the only
//  thing the collision system asks of a collider.
//
//  Five shapes, one function each:
//
//    BoxCollider        1 shape   a rectangle, ROTATED PROPERLY (see below)
//    CircleCollider     1 shape   a centre and a radius
//    CapsuleCollider    1 shape   a rectangle with round ends
//    PolygonCollider    1 shape   up to 8 points, convex
//    EdgeCollider       n shapes  a chain of segments, one shape per segment
//
//  The edge collider is why WorldShapes returns a COUNT rather than a single
//  shape. A polyline is not convex and cannot be one ConvexShape, but it is
//  trivially a list of them - and every other collider simply returns 1.
//
//  ==========================================================================
//  ROTATION IS NOW REAL, which it was not before.
//
//  A box used to be tested as the upright box that SURROUNDED the rotated one
//  - up to about 40% too big at 45 degrees. It fired early, in a way that got
//  worse the more you rotated something, and it could not be fixed without
//  oriented-box maths. Overlap.h has that maths now, so a rotated box is a
//  rotated box.
//
//  ONE SIMPLIFICATION REMAINS, STATED RATHER THAN HIDDEN: a circle scaled
//  unevenly - say 2 on x and 1 on y - is an ellipse, and there is no ellipse
//  test here. The LARGER scale is used, so it comes out slightly too big
//  rather than too small. Use a capsule or a polygon when that matters.
//
//  ==========================================================================
//  LAYERS: WHAT SOMETHING IS, AND WHAT IT CARES ABOUT
//
//  Every collider has:
//    * a LAYER        - one name saying what it is: "Player", "Pickup", "World"
//    * a COLLIDES-WITH list - the layers it wants to hear about
//
//  A pair is only tested when BOTH sides are interested in each other. That is
//  deliberate. "Either side is enough" would let the player receive a
//  collision from a wall while the wall received nothing, and one-sided events
//  are the kind of bug where the first hour is spent not believing it.
//
//  THIS IS DELIBERATELY NOT UNITY'S LAYER COLLISION MATRIX. Unity keeps one
//  global grid of which layers touch which, in project settings, away from the
//  objects it governs. Naming the layers on the collider means a scene file
//  says everything about how its own objects interact, with nothing to look up
//  elsewhere - which matters far more for reading somebody else's scene than
//  the matrix's brevity does.
//
//  ==========================================================================
//  TRIGGERS AND SOLIDS ARE GENUINELY DIFFERENT THINGS
//
//  A TRIGGER reports overlaps and nothing else. Things pass straight through.
//  A SOLID collider is pushed apart - if at least one side has a Rigidbody2D
//  to be pushed. Two solid colliders with no rigidbody between them are both
//  immovable scenery, and nothing happens, exactly as in Unity.
//
//  They send DIFFERENT EVENTS, and this is the thing to know when porting an
//  old script:
//
//      either side is a trigger  ->  OnTriggerEnter / Stay / Exit
//      neither side is a trigger ->  OnCollisionEnter / Stay / Exit
//
//  A script with OnCollisionEnter on a trigger collider will never be called.
//  Because that is silent - the hook is a real hook, just not the one being
//  sent - the collision system REPORTS IT as an error the first time it
//  happens, naming the entity and the hook to use instead.
// ============================================================================

#include <engine/math/Mat3.h>
#include <engine/math/Overlap.h>
#include <engine/physics/Contact.h>
#include <engine/scene/Component.h>
#include <engine/scene/EntityId.h>
#include <engine/scene/SystemOrder.h>

#include <string>
#include <vector>

namespace eng {

class Rigidbody2DComponent;

// The special layer name meaning "everything".
inline constexpr const char* kCollisionLayerAll = "All";

// ---------------------------------------------------------------------------
//  PhysicsMaterial2D - how a surface behaves when something hits it.
//
//  Unity keeps this in a separate asset file that colliders point at. Here it
//  is two numbers on the collider itself, because an asset type exists to be
//  SHARED between many objects and a project with four surfaces in it gains
//  nothing from the indirection but a file to go and open.
// ---------------------------------------------------------------------------
struct PhysicsMaterial2D {
    // 0 is ice, 1 is rubber. How much sideways grip a contact has.
    float friction = 0.4f;

    // 0 is a beanbag, 1 is a superball. How much of the approach speed comes
    // back. Above 1 a collision ADDS energy and the object climbs forever,
    // which is why it is clamped when read.
    float bounciness = 0.0f;
};

// Which of the two numbers below is the long axis, matching Unity's
// CapsuleDirection2D.
enum class CapsuleDirection { Vertical, Horizontal };

enum class ColliderShape { Box, Circle, Capsule, Polygon, Edge, Tilemap };

// ---------------------------------------------------------------------------
//  What every collider has in common.
// ---------------------------------------------------------------------------
class ColliderComponent : public Component {
public:
    // The size of the STACK BUFFER a caller can get away with without asking,
    // which is a convenience and not a limit. Anything that might hold more -
    // a long edge chain, a tilemap - is gathered through GatherWorldShapes
    // below, which asks WorldShapeCount how much room it needs.
    static constexpr int kMaxWorldShapes = 32;

    // Shared scene file fields:
    //   "layer":        "Player"
    //   "collidesWith": ["Pickup", "World"]     or "All"
    //   "trigger":      true
    //   "offset":       [0, 0]
    //   "enabled":      true
    //   "friction":     0.4
    //   "bounciness":   0.0
    //   "density":      1.0
    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;
    void OnAttach() override;
    void OnDetach() override;

    virtual ColliderShape Shape() const = 0;

    // THE ONE THING THE COLLISION SYSTEM ASKS FOR: this collider as geometry,
    // in world space. Returns how many shapes were written, never more than
    // `maxOut`.
    //
    // Most colliders write exactly 1. An edge collider writes one per segment,
    // and a tilemap collider one per merged run of solid cells.
    virtual int WorldShapes(ConvexShape* out, int maxOut) const = 0;

    // How many shapes WorldShapes would write given unlimited room.
    //
    // THIS EXISTS SO A CALLER CAN SIZE ITS BUFFER BEFORE ASKING. Without it the
    // only safe thing to do is guess, and a guess that is too small does not
    // fail - it silently returns part of the collider. A tilemap missing two
    // thirds of its walls is a bug that looks like bad level design.
    //
    // The default of 1 is right for every single-shape collider, which is why
    // only the two that can produce more override it.
    virtual int WorldShapeCount() const { return 1; }

    // The area of the shape, for a rigidbody working its mass out from a
    // density. Approximate for a capsule, which is close enough for a number
    // whose job is to make heavy things feel heavy.
    virtual float Area() const = 0;

    // The upright box this collider occupies in the world. Built from
    // WorldShapes, so a collider cannot have bounds that disagree with its own
    // geometry - which is the bug that makes a broad phase drop real contacts.
    AABB WorldBounds() const;

    const std::string&              Layer() const        { return m_layer; }
    const std::vector<std::string>& CollidesWith() const { return m_collidesWith; }

    void SetLayer(std::string_view layer) { m_layer = std::string(layer); }
    void SetCollidesWith(std::vector<std::string> layers) {
        m_collidesWith = std::move(layers);
    }

    // True when this collider wants to know about overlaps with `layer`.
    bool CaresAbout(const std::string& layer) const;

    // A trigger reports overlaps but nothing is pushed apart. Almost every
    // game wants at least one - a doorway, a checkpoint, a pickup.
    bool IsTrigger() const { return m_trigger; }
    void SetTrigger(bool trigger) { m_trigger = trigger; }

    // Switched off, a collider keeps all its settings and takes part in
    // nothing: no events, no pushing, invisible to every query. This is how a
    // door is opened or a hitbox is active for three frames of an attack.
    bool Enabled() const { return m_enabled; }
    void SetEnabled(bool enabled) { m_enabled = enabled; }

    // Shifts the shape away from the entity's own position, so a character's
    // feet can be the part that collides.
    Vec2 Offset() const { return m_offset; }
    void SetOffset(Vec2 offset) { m_offset = offset; }

    const PhysicsMaterial2D& Material() const { return m_material; }
    void SetMaterial(const PhysicsMaterial2D& material) { m_material = material; }

    float Density() const { return m_density; }
    void  SetDensity(float density);

    // The rigidbody that moves this collider. Null when nothing can move it,
    // which is what makes a collider static scenery.
    //
    // ON THIS ENTITY ONLY, and that is a stated difference from Unity, which
    // also searches up the parent chain. The engine's Transform2D does not
    // know which entity owns it, so walking up from a collider to a parent's
    // components is not a lookup that exists - it would mean scanning the
    // scene for whichever entity holds that transform, once per collider, per
    // step.
    //
    // In practice a collider and its rigidbody live on the same entity in
    // almost every 2D setup. For a compound body - several colliders making up
    // one physical object - put each collider on the entity that has the
    // rigidbody rather than on children.
    Rigidbody2DComponent* AttachedRigidbody() const;

    // The point on this collider nearest `worldPoint`. Unity's
    // Collider2D.ClosestPoint. For a point inside the collider, the nearest
    // point on the boundary.
    Vec2 ClosestPoint(Vec2 worldPoint) const;

    // Is this world-space point inside the collider? Unity's
    // Collider2D.OverlapPoint.
    bool OverlapPoint(Vec2 worldPoint) const;

protected:
    // The entity's world matrix, or the identity when it has no transform.
    // Every collider needs it and nobody should re-derive the null check.
    Mat3 OwnerWorldMatrix() const;

    std::string              m_layer = "Default";
    std::vector<std::string> m_collidesWith{kCollisionLayerAll};
    Vec2                     m_offset{0.0f, 0.0f};
    bool                     m_trigger = false;
    bool                     m_enabled = true;
    PhysicsMaterial2D        m_material{};
    float                    m_density = 1.0f;
};

// ---------------------------------------------------------------------------
//  A rectangle. Unity's BoxCollider2D.
// ---------------------------------------------------------------------------
class BoxColliderComponent final : public ColliderComponent {
public:
    static constexpr const char* kTypeName = "BoxColliderComponent";

    // The name this component had when a box was always axis-aligned. Scene
    // files written then still load, because the factory registers this name
    // too - see CollisionSystem::RegisterComponentTypes.
    static constexpr const char* kLegacyTypeName = "AABBColliderComponent";

    const char*   TypeName() const override { return kTypeName; }
    ColliderShape Shape() const override    { return ColliderShape::Box; }

    // Extra scene file fields:
    //   "halfExtents": [w, h]   half the width and half the height
    //   "size":        [w, h]   the whole width and height - Unity's spelling,
    //                           accepted on read as a convenience
    //   "edgeRadius":  0        rounds the corners off, as Unity's does
    bool  Deserialize(const Json& node, std::string& outError) override;
    bool  Serialize(Json& out) const override;
    int   WorldShapes(ConvexShape* out, int maxOut) const override;
    float Area() const override;

    Vec2 HalfExtents() const { return m_halfExtents; }
    void SetHalfExtents(Vec2 halfExtents) { m_halfExtents = halfExtents; }

    // The same rectangle said Unity's way. Stored as half-extents because that
    // is what the maths wants; offered both ways because that is what people
    // type.
    Vec2 Size() const { return m_halfExtents * 2.0f; }
    void SetSize(Vec2 size) { m_halfExtents = size * 0.5f; }

    float EdgeRadius() const { return m_edgeRadius; }
    void  SetEdgeRadius(float radius);

private:
    Vec2  m_halfExtents{0.5f, 0.5f};
    float m_edgeRadius = 0.0f;
};

// The name this class used to have. Kept so that scripts and editor code
// written against it still compile.
using AABBColliderComponent = BoxColliderComponent;

// ---------------------------------------------------------------------------
//  A circle. Unity's CircleCollider2D.
// ---------------------------------------------------------------------------
class CircleColliderComponent final : public ColliderComponent {
public:
    static constexpr const char* kTypeName = "CircleColliderComponent";

    const char*   TypeName() const override { return kTypeName; }
    ColliderShape Shape() const override    { return ColliderShape::Circle; }

    // Extra scene file field:
    //   "radius": 16
    bool  Deserialize(const Json& node, std::string& outError) override;
    bool  Serialize(Json& out) const override;
    int   WorldShapes(ConvexShape* out, int maxOut) const override;
    float Area() const override;

    Circle WorldCircle() const;

    float Radius() const { return m_radius; }
    void  SetRadius(float radius) { m_radius = radius; }

private:
    float m_radius = 0.5f;
};

// ---------------------------------------------------------------------------
//  A rectangle with round ends. Unity's CapsuleCollider2D.
//
//  Described exactly as Unity describes it: a SIZE and which way round it is.
//  The radius is half the short side, and the core segment is however much
//  length is left over once both round ends are accounted for:
//
//      size (1, 3), vertical  ->  radius 0.5, core segment 2 long
//      size (1, 1), vertical  ->  radius 0.5, core segment 0 - a circle
//
//  This is the shape a character controller usually wants. A box catches on
//  every seam in the floor; a capsule slides over them.
// ---------------------------------------------------------------------------
class CapsuleColliderComponent final : public ColliderComponent {
public:
    static constexpr const char* kTypeName = "CapsuleColliderComponent";

    const char*   TypeName() const override { return kTypeName; }
    ColliderShape Shape() const override    { return ColliderShape::Capsule; }

    // Extra scene file fields:
    //   "size":      [1, 2]
    //   "direction": "Vertical"   or "Horizontal"
    bool  Deserialize(const Json& node, std::string& outError) override;
    bool  Serialize(Json& out) const override;
    int   WorldShapes(ConvexShape* out, int maxOut) const override;
    float Area() const override;

    Vec2 Size() const { return m_size; }
    void SetSize(Vec2 size);

    CapsuleDirection Direction() const { return m_direction; }
    void SetDirection(CapsuleDirection direction) { m_direction = direction; }

private:
    Vec2             m_size{1.0f, 2.0f};
    CapsuleDirection m_direction = CapsuleDirection::Vertical;
};

// ---------------------------------------------------------------------------
//  A polygon. Unity's PolygonCollider2D, with one honest difference.
//
//  UNITY'S IS CONCAVE AND MULTI-PATH; THIS ONE IS A SINGLE CONVEX SHAPE of up
//  to 8 points. Concave collision needs the shape broken into convex pieces
//  first, and doing that automatically is a real algorithm with real failure
//  cases on self-intersecting input.
//
//  What to do instead is put several colliders on the entity - they are tested
//  independently and the result is the union, which is exactly what Unity's
//  decomposition produces anyway. The editor says so when a shape is not
//  convex rather than letting it collide wrongly.
// ---------------------------------------------------------------------------
class PolygonColliderComponent final : public ColliderComponent {
public:
    static constexpr const char* kTypeName = "PolygonColliderComponent";

    const char*   TypeName() const override { return kTypeName; }
    ColliderShape Shape() const override    { return ColliderShape::Polygon; }

    // Extra scene file field:
    //   "points": [[x,y], [x,y], ...]    in the entity's own space,
    //                                    anticlockwise
    bool  Deserialize(const Json& node, std::string& outError) override;
    bool  Serialize(Json& out) const override;
    int   WorldShapes(ConvexShape* out, int maxOut) const override;
    float Area() const override;

    const std::vector<Vec2>& Points() const { return m_points; }

    // Replaces the outline. Points beyond ConvexShape::kMaxPoints are dropped
    // and reported.
    void SetPoints(std::vector<Vec2> points);

    // Is the outline convex and wound anticlockwise? The collision maths
    // assumes both, and a shape that fails this collides as its convex hull
    // rather than as drawn - so the editor shows the answer.
    bool IsConvex() const;

    // A square, as a starting point for a new collider. A polygon collider
    // with no points at all cannot be dragged into shape.
    static std::vector<Vec2> DefaultSquare();

private:
    std::vector<Vec2> m_points = DefaultSquare();
};

// ---------------------------------------------------------------------------
//  A chain of line segments. Unity's EdgeCollider2D.
//
//  This is the one for ground. A platform built from an edge collider has no
//  underside and no ends to catch on, so a character cannot clip up through
//  the floor or snag on the join between two tiles.
//
//  It is OPEN by default - the last point does not join back to the first -
//  which is the difference between a hillside and a closed loop.
// ---------------------------------------------------------------------------
class EdgeColliderComponent final : public ColliderComponent {
public:
    static constexpr const char* kTypeName = "EdgeColliderComponent";

    // How long a chain may be: 32 segments, so 33 points. A generous piece of
    // terrain, and past it the tail is dropped and REPORTED rather than
    // silently ignored.
    //
    // Its own constant rather than kMaxWorldShapes, which it used to borrow.
    // That number is now only the size of a convenience stack buffer, and
    // reading a real limit off it would make the two move together for no
    // reason.
    static constexpr int kMaxEdgePoints = 33;

    const char*   TypeName() const override { return kTypeName; }
    ColliderShape Shape() const override    { return ColliderShape::Edge; }

    // Extra scene file fields:
    //   "points":     [[x,y], ...]   in the entity's own space
    //   "edgeRadius": 0              gives the line thickness
    //   "loop":       false          joins the last point back to the first
    bool  Deserialize(const Json& node, std::string& outError) override;
    bool  Serialize(Json& out) const override;
    int   WorldShapes(ConvexShape* out, int maxOut) const override;
    int   WorldShapeCount() const override;
    float Area() const override;

    const std::vector<Vec2>& Points() const { return m_points; }
    void SetPoints(std::vector<Vec2> points);

    float EdgeRadius() const { return m_edgeRadius; }
    void  SetEdgeRadius(float radius);

    bool Loop() const { return m_loop; }
    void SetLoop(bool loop) { m_loop = loop; }

    static std::vector<Vec2> DefaultLine();

private:
    std::vector<Vec2> m_points = DefaultLine();
    float             m_edgeRadius = 0.0f;
    bool              m_loop = false;
};


// ---------------------------------------------------------------------------
//  Every world shape of a collider, however many that turns out to be.
//
//  `into` is resized to fit and then filled; the return value is how many were
//  written, which is also into.size(). PASS THE SAME VECTOR BACK IN on every
//  call in a loop - it keeps whatever capacity it reached, so a per-frame
//  gather over hundreds of colliders allocates once rather than every time.
//
//  USE THIS RATHER THAN A STACK ARRAY whenever the collider is not known to be
//  a single-shape one. A tilemap collider routinely produces hundreds of
//  boxes, and a fixed buffer would quietly return the first 32 of them.
// ---------------------------------------------------------------------------
int GatherWorldShapes(const ColliderComponent& collider,
                      std::vector<ConvexShape>& into);

// ---------------------------------------------------------------------------
//  The first collider on an entity, whatever shape it happens to be - or null.
//
//  Entity::Find<T>() matches on a TYPE NAME, so it can find a box collider or
//  a circle collider but cannot answer "does this thing have any collider at
//  all". With one shape that distinction did not exist; with five it is the
//  question most game code actually wants:
//
//      void OnTriggerEnter(eng::EntityId other) {
//          eng::Entity* partner = GetScene()->Get(other);
//          const auto* collider = eng::FindCollider(*partner);
//          if (collider != nullptr && collider->Layer() == "Player") { ... }
//      }
//
//  Asking for a BOX there would work right up until somebody gave the player
//  a capsule, and then fail silently.
// ---------------------------------------------------------------------------
ColliderComponent* FindCollider(Entity& entity);

// ---------------------------------------------------------------------------
//  Checks colliders against each other once per simulation step, pushes the
//  solid ones apart, and sends the enter/stay/exit events.
//
//  It runs at stage 400 (Collision) - AFTER rigidbody integration at 350 and
//  script movement at 200, so things are tested where they moved to this tick
//  rather than where they were last tick.
// ---------------------------------------------------------------------------
class CollisionSystem final : public System {
public:
    void        Update(float deltaSeconds) override;
    const char* Name() const override  { return "CollisionSystem"; }
    int         Order() const override { return SystemStage::kCollision; }

    static void Register(ColliderComponent& collider);
    static void Unregister(ColliderComponent& collider);
    static void Clear();

    static std::size_t ColliderCount();
    static std::size_t ActivePairCount();   // how many pairs are touching now

    // How many pairs the narrow phase actually had to look at this step, and
    // how many it would have looked at without a broad phase. The editor shows
    // both, because "the broad phase is working" is otherwise invisible.
    static std::size_t NarrowPhaseTests();
    static std::size_t BroadPhasePairs();
    static std::size_t BruteForcePairs();

    // The contact for a touching pair, or null. This is how the script
    // dispatcher turns a bus message into a Collision2D without the contact
    // data having to travel through the message itself.
    static const Collision2D* FindContact(EntityId a, EntityId b);

    // Every collider currently attached, for the queries in Physics2D.
    static const std::vector<ColliderComponent*>& All();

    // How many solver passes each step makes over the contact list. More is
    // steadier when things are stacked; 1 is enough for a single character on
    // a floor. Clamped to a sane range.
    static void SetSolverIterations(int iterations);
    static int  SolverIterations();

    static void RegisterComponentTypes();
};

} // namespace eng
