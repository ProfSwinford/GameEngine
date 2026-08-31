#pragma once

// =============================================================================
//  WEEK 10 - collider components and the collision system.
//
//  *** NO NEW INTERSECTION MATH WAS WRITTEN THIS WEEK. ***
//
//  Week 6 already provides Overlaps(AABB, AABB), Overlaps(Circle, Circle) and
//  Overlaps(AABB, Circle) as pure, fully unit-tested functions including the
//  touching-edge and containment cases. This file wraps them in COMPONENTS,
//  LAYERS and EVENTS and calls them. That separation is why Week 6 insisted
//  the overlap functions be pure, and this is the payoff.
//
// =============================================================================
//  LAYERS AND MASKS.
//
//    A collider's LAYER is what it IS         - one bit.
//    A collider's MASK is what it CARES about - many bits.
//
//  *** BOTH MASKS MUST MATCH. *** A pair is tested only if
//      (a.mask & b.layer) && (b.mask & a.layer).
//
//  "Either" was rejected: it means A can be interested in B while B ignores A,
//  which produces ONE-SIDED events - the player gets a CollisionEnter and the
//  wall does not - and every gameplay bug that follows from that starts with
//  someone not believing it is happening. "Both" is symmetric, so the truth
//  table has two rows instead of four and the events always come in pairs.
//
//  The cost of "both" is that a one-way trigger has to be spelled out on both
//  sides. That is a line of config, not a mystery.
//
// =============================================================================
//  ENTER / STAY / EXIT is a diff of this frame's overlapping pairs against
//  last frame's:
//      in this, not in last -> ENTER
//      in both              -> STAY
//      in last, not in this -> EXIT
//
//  *** EXIT ON DESTRUCTION: YES, AN EXIT IS FIRED. ***
//
//  When an entity is destroyed there is no collider to test, so the pair
//  simply disappears from this frame's set and the diff would produce an exit
//  anyway - but only if the SURVIVING entity is still there to receive it, and
//  only if the message is not dropped for naming a dead target. So the exit is
//  sent explicitly to the survivor, with the dead entity as the partner, and
//  the handler is expected to check validity before dereferencing.
//
//  The alternative - no exit on destruction - breaks the single most common
//  trigger pattern there is: open a door on enter, close it on exit. Destroy
//  the key while it is inside the volume and the door stays open forever.
//
// =============================================================================
//  ROTATED PARENTS: an axis-aligned CHILD box is not axis-aligned in world
//  space once an ancestor rotates. THIS ENGINE USES THE AXIS-ALIGNED BOUNDS OF
//  THE ROTATED BOX - stated, as the header asks, rather than left implicit.
//  It over-approximates by up to 41% on a 45-degree rotation, which produces
//  collisions that fire slightly early rather than slightly late. Early is the
//  safe direction for gameplay. A rotated-box test via separating axes is the
//  Week 6 stretch goal and is the upgrade path.
//
//  BROAD PHASE: none. Every pair against every other pair, O(n^2), and that is
//  FINE for this week and for most Phase 2 games. The profiler HUD line item
//  built this week is how the decision to build one gets made - by measurement,
//  not because a grid sounds impressive.
// =============================================================================

#include <engine/math/Overlap.h>
#include <engine/scene/Component.h>
#include <engine/scene/SystemOrder.h>

#include <vector>

namespace eng {

using CollisionLayer = u32;

// Named layers, so a scene file can say "Player" instead of 2. Bit 0 is
// Default, which every collider gets unless it says otherwise.
namespace CollisionLayers {
inline constexpr CollisionLayer kDefault   = 1u << 0;
inline constexpr CollisionLayer kPlayer    = 1u << 1;
inline constexpr CollisionLayer kEnemy     = 1u << 2;
inline constexpr CollisionLayer kPickup    = 1u << 3;
inline constexpr CollisionLayer kProjectile = 1u << 4;
inline constexpr CollisionLayer kWorld     = 1u << 5;
inline constexpr CollisionLayer kTrigger   = 1u << 6;
inline constexpr CollisionLayer kAll       = 0xFFFFFFFFu;

CollisionLayer FromName(std::string_view name);
const char*    Name(u32 bitIndex);
inline constexpr u32 kNamedLayerCount = 7;
} // namespace CollisionLayers

enum class ColliderShape : u8 { Box, Circle };

class ColliderComponent : public Component {
public:
    bool Deserialize(const ConfigNode& node, std::string& outError) override;
    bool Serialize(ConfigWriter& out) const override;
    void OnAttach() override;
    void OnDetach() override;

    virtual ColliderShape Shape() const = 0;

    CollisionLayer Layer() const { return m_layer; }
    CollisionLayer Mask() const  { return m_mask; }
    void SetLayer(CollisionLayer layer) { m_layer = layer; }
    void SetMask(CollisionLayer mask)   { m_mask = mask; }

    // A trigger detects and reports overlap but does not resolve it. Almost
    // every Phase 2 game wants one, and it is a bool.
    bool IsTrigger() const { return m_trigger; }
    void SetTrigger(bool trigger) { m_trigger = trigger; }

    Vec2 Offset() const { return m_offset; }
    void SetOffset(Vec2 offset) { m_offset = offset; }

    // World-space axis-aligned bounds, computed through the Week 6 transform
    // hierarchy. See the rotated-parent note above.
    virtual AABB WorldBounds() const = 0;

protected:
    CollisionLayer m_layer   = CollisionLayers::kDefault;
    CollisionLayer m_mask    = CollisionLayers::kAll;
    Vec2           m_offset{0.0f, 0.0f};
    bool           m_trigger = false;
};

class AABBColliderComponent final : public ColliderComponent {
public:
    static constexpr const char* kTypeName = "AABBColliderComponent";
    static StringId TypeIdStatic();

    StringId      TypeId() const override { return TypeIdStatic(); }
    const char*   TypeName() const override { return kTypeName; }
    ColliderShape Shape() const override { return ColliderShape::Box; }

    bool Deserialize(const ConfigNode& node, std::string& outError) override;
    bool Serialize(ConfigWriter& out) const override;
    AABB WorldBounds() const override;

    Vec2 HalfExtents() const { return m_halfExtents; }
    void SetHalfExtents(Vec2 halfExtents) { m_halfExtents = halfExtents; }

private:
    Vec2 m_halfExtents{0.5f, 0.5f};
};

class CircleColliderComponent final : public ColliderComponent {
public:
    static constexpr const char* kTypeName = "CircleColliderComponent";
    static StringId TypeIdStatic();

    StringId      TypeId() const override { return TypeIdStatic(); }
    const char*   TypeName() const override { return kTypeName; }
    ColliderShape Shape() const override { return ColliderShape::Circle; }

    bool Deserialize(const ConfigNode& node, std::string& outError) override;
    bool Serialize(ConfigWriter& out) const override;
    AABB WorldBounds() const override;
    Circle WorldCircle() const;

    f32  Radius() const { return m_radius; }
    void SetRadius(f32 radius) { m_radius = radius; }

private:
    f32 m_radius = 0.5f;
};

// The collision system. A System, so it slots into the declared order at
// stage 400 and gets its own scoped timer - which is what puts collision on
// the profiler HUD as its own line item, a Week 10 verification.
class CollisionSystem final : public System {
public:
    void        Update(f32 deltaSeconds) override;
    const char* Name() const override { return "CollisionSystem"; }
    i32         Order() const override { return SystemStage::kCollision; }

    static void Register(ColliderComponent& collider);
    static void Unregister(ColliderComponent& collider);
    static void Clear();

    static usize ColliderCount();
    static usize ActivePairCount();     // overlapping right now
    static u64   PairTestsLastFrame();  // the O(n^2) number, for the HUD
    static u64   TotalEnterEvents();

    // Registers AABBColliderComponent and CircleColliderComponent with the
    // component factory. Called alongside ComponentFactory::RegisterBuiltins.
    static void RegisterComponentTypes();
};

} // namespace eng
