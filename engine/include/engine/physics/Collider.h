#pragma once

// ============================================================================
//  Collider.h - the shapes entities collide with, and the system that checks
//  them.
//
//  NO COLLISION MATHS IS WRITTEN IN THIS FILE. math/Overlap.h already answers
//  "are these two shapes touching?" for boxes and circles. All this file does
//  is wrap those answers in components, layers and events. Keeping the maths
//  as plain functions over there is what makes that possible.
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
//  The cost of requiring both is that a one-way trigger has to be spelled out
//  on both sides. That is one line in a scene file, not a mystery.
//
//  ==========================================================================
//  ENTER, STAY AND EXIT are worked out by comparing which pairs are touching
//  this tick against which were touching last tick:
//
//      touching now, not before  ->  CollisionEnter
//      touching now and before   ->  CollisionStay
//      was touching, not now     ->  CollisionExit
//
//  AN EXIT IS ALSO SENT WHEN ONE SIDE IS DESTROYED. Without it, the single
//  most common trigger pattern - open a door on enter, close it on exit -
//  breaks the moment the key is destroyed while it is still inside the volume:
//  the door stays open forever.
//
//  ==========================================================================
//  TWO SIMPLIFICATIONS, STATED RATHER THAN HIDDEN
//
//  1. A BOX ATTACHED TO A ROTATED PARENT is tested using the upright box that
//     SURROUNDS the rotated one. That is up to about 40% too big at a
//     45-degree angle, so collisions fire slightly EARLY rather than slightly
//     late - which is the safer direction for a game.
//
//  2. EVERY COLLIDER IS TESTED AGAINST EVERY OTHER ONE. With a few dozen
//     objects that is perfectly fine. A real game with thousands would first
//     divide the world into a grid so that far-apart objects are never
//     compared - but doing that before it is needed is work spent on a problem
//     nobody has.
// ============================================================================

#include <engine/math/Overlap.h>
#include <engine/scene/Component.h>
#include <engine/scene/SystemOrder.h>

#include <string>
#include <vector>

namespace eng {

// The special layer name meaning "everything".
inline constexpr const char* kCollisionLayerAll = "All";

enum class ColliderShape { Box, Circle };

// What every collider has in common. AABBColliderComponent and
// CircleColliderComponent both inherit from this and add their own shape.
class ColliderComponent : public Component {
public:
    // Shared scene file fields:
    //   "layer":       "Player"
    //   "collidesWith": ["Pickup", "World"]     or "All"
    //   "trigger":     true
    //   "offset":      [0, 0]
    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;
    void OnAttach() override;
    void OnDetach() override;

    virtual ColliderShape Shape() const = 0;

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

    // Shifts the shape away from the entity's own position, so a character's
    // feet can be the part that collides.
    Vec2 Offset() const { return m_offset; }
    void SetOffset(Vec2 offset) { m_offset = offset; }

    // The upright box this collider occupies in the world, worked out through
    // the transform hierarchy. See simplification 1 above.
    virtual AABB WorldBounds() const = 0;

protected:
    std::string              m_layer = "Default";
    std::vector<std::string> m_collidesWith{kCollisionLayerAll};
    Vec2                     m_offset{0.0f, 0.0f};
    bool                     m_trigger = false;
};

// A rectangle.
class AABBColliderComponent final : public ColliderComponent {
public:
    static constexpr const char* kTypeName = "AABBColliderComponent";

    const char*   TypeName() const override { return kTypeName; }
    ColliderShape Shape() const override    { return ColliderShape::Box; }

    // Extra scene file field:
    //   "halfExtents": [w, h]    half the width and half the height
    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;
    AABB WorldBounds() const override;

    Vec2 HalfExtents() const { return m_halfExtents; }
    void SetHalfExtents(Vec2 halfExtents) { m_halfExtents = halfExtents; }

private:
    Vec2 m_halfExtents{0.5f, 0.5f};
};

// A circle.
class CircleColliderComponent final : public ColliderComponent {
public:
    static constexpr const char* kTypeName = "CircleColliderComponent";

    const char*   TypeName() const override { return kTypeName; }
    ColliderShape Shape() const override    { return ColliderShape::Circle; }

    // Extra scene file field:
    //   "radius": 16
    bool   Deserialize(const Json& node, std::string& outError) override;
    bool   Serialize(Json& out) const override;
    AABB   WorldBounds() const override;
    Circle WorldCircle() const;

    float Radius() const { return m_radius; }
    void  SetRadius(float radius) { m_radius = radius; }

private:
    float m_radius = 0.5f;
};

// Checks every collider against every other one, once per simulation step, and
// sends the enter/stay/exit messages.
//
// It runs at stage 400 (Collision) - AFTER movement at 300, so things are
// tested where they moved to this tick rather than where they were last tick.
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

    static void RegisterComponentTypes();
};

} // namespace eng
