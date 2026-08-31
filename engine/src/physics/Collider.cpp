// WEEK 10 - colliders and the collision system. See Collider.h for the four
// recorded decisions (both-masks, exit-on-destroy, rotated bounds, no broad
// phase).
//
// Note that this file calls Overlaps() and never implements it.

#include <engine/core/CVar.h>
#include <engine/core/Log.h>
#include <engine/debug/DebugDraw.h>
#include <engine/physics/Collider.h>
#include <engine/scene/DeferredOps.h>
#include <engine/scene/Messaging.h>
#include <engine/scene/Scene.h>

#include <algorithm>
#include <set>
#include <vector>

namespace eng {
namespace {

// The dense array the system walks - same shape argument as SpriteRecord in
// Component.h, for the same reason.
struct ColliderRecord {
    ColliderComponent* collider = nullptr;
    EntityHandle       owner{};
};

std::vector<ColliderRecord> g_colliders;

// A pair of entity handles, always stored with the smaller handle first so
// that (a,b) and (b,a) are the same key. Forgetting that is how you get an
// enter event every frame - the set never matches because the order flipped.
using Pair = std::pair<u32, u32>;

std::set<Pair> g_lastFrame;
std::set<Pair> g_thisFrame;

u64 g_pairTests    = 0;
u64 g_enterEvents  = 0;

Pair MakePair(EntityHandle a, EntityHandle b) {
    return (a.value < b.value) ? Pair{a.value, b.value} : Pair{b.value, a.value};
}

// BOTH masks must match. See the header for why "either" was rejected.
bool LayersInterested(const ColliderComponent& a, const ColliderComponent& b) {
    return (a.Mask() & b.Layer()) != 0 && (b.Mask() & a.Layer()) != 0;
}

bool ShapesOverlap(const ColliderComponent& a, const ColliderComponent& b) {
    const bool aIsCircle = a.Shape() == ColliderShape::Circle;
    const bool bIsCircle = b.Shape() == ColliderShape::Circle;

    // Every branch is a Week 6 function call. No new math.
    if (aIsCircle && bIsCircle) {
        return Overlaps(static_cast<const CircleColliderComponent&>(a).WorldCircle(),
                        static_cast<const CircleColliderComponent&>(b).WorldCircle());
    }
    if (aIsCircle) {
        return Overlaps(b.WorldBounds(),
                        static_cast<const CircleColliderComponent&>(a).WorldCircle());
    }
    if (bIsCircle) {
        return Overlaps(a.WorldBounds(),
                        static_cast<const CircleColliderComponent&>(b).WorldCircle());
    }
    return Overlaps(a.WorldBounds(), b.WorldBounds());
}

void SendPairEvent(StringId type, EntityHandle to, EntityHandle partner) {
    Message message;
    message.type   = type;
    message.target = to;
    message.sender = partner;
    message.other  = partner;
    MessageBus::Send(message);
}

CVar* g_drawColliders = nullptr;

} // namespace

namespace CollisionLayers {

CollisionLayer FromName(std::string_view name) {
    if (name == "Default")    { return kDefault; }
    if (name == "Player")     { return kPlayer; }
    if (name == "Enemy")      { return kEnemy; }
    if (name == "Pickup")     { return kPickup; }
    if (name == "Projectile") { return kProjectile; }
    if (name == "World")      { return kWorld; }
    if (name == "Trigger")    { return kTrigger; }
    if (name == "All")        { return kAll; }
    return 0;
}

const char* Name(u32 bitIndex) {
    static const char* kNames[] = {"Default", "Player", "Enemy",  "Pickup",
                                   "Projectile", "World", "Trigger"};
    return (bitIndex < kNamedLayerCount) ? kNames[bitIndex] : "?";
}

} // namespace CollisionLayers

// ---------------------------------------------------------------------------
//  ColliderComponent
// ---------------------------------------------------------------------------

bool ColliderComponent::Deserialize(const ConfigNode& node, std::string& outError) {
    // Layers are named in the file, never numbered: "layer": "Enemy" survives
    // someone inserting a new layer, and "layer": 4 does not.
    if (const ConfigNode layer = node.Child("layer"); layer.IsValid()) {
        const std::string name = layer.AsString("Default");
        const CollisionLayer bit = CollisionLayers::FromName(name);
        if (bit == 0) {
            outError = layer.Path() + ": unknown collision layer '" + name + "'";
            return false;
        }
        m_layer = bit;
    }

    if (const ConfigNode mask = node.Child("mask"); mask.IsValid()) {
        if (mask.IsString()) {
            const std::string name = mask.AsString("All");
            const CollisionLayer bit = CollisionLayers::FromName(name);
            if (bit == 0) {
                outError = mask.Path() + ": unknown collision layer '" + name + "'";
                return false;
            }
            m_mask = bit;
        } else if (mask.IsArray()) {
            m_mask = 0;
            for (usize i = 0; i < mask.Size(); ++i) {
                const std::string name = mask.At(i).AsString("");
                const CollisionLayer bit = CollisionLayers::FromName(name);
                if (bit == 0) {
                    outError = mask.At(i).Path() + ": unknown collision layer '" + name + "'";
                    return false;
                }
                m_mask |= bit;
            }
        } else {
            outError = mask.Path() + " must be a layer name or an array of layer names";
            return false;
        }
    }

    m_trigger = node.Child("trigger").AsBool(false);

    if (const ConfigNode offset = node.Child("offset"); offset.IsValid()) {
        f32 xy[2] = {0.0f, 0.0f};
        if (!offset.AsFloatArray(xy, 2)) {
            outError = offset.Path() + " must be an array of two numbers";
            return false;
        }
        m_offset = Vec2{xy[0], xy[1]};
    }
    return true;
}

bool ColliderComponent::Serialize(ConfigWriter& out) const {
    // Layers are written back as NAMES, never numbers - the same reason
    // Deserialize insists on reading them that way. "layer": "Enemy" survives
    // someone inserting a new layer into the enum; "layer": 4 does not.
    for (u32 bit = 0; bit < CollisionLayers::kNamedLayerCount; ++bit) {
        if (m_layer == (1u << bit)) {
            out.SetString("layer", CollisionLayers::Name(bit));
            break;
        }
    }

    if (m_mask == CollisionLayers::kAll) {
        out.SetString("mask", "All");
    } else {
        std::vector<std::string> names;
        for (u32 bit = 0; bit < CollisionLayers::kNamedLayerCount; ++bit) {
            if ((m_mask & (1u << bit)) != 0) {
                names.emplace_back(CollisionLayers::Name(bit));
            }
        }
        out.SetStringArray("mask", names);
    }

    out.SetBool("trigger", m_trigger);

    if (m_offset.x != 0.0f || m_offset.y != 0.0f) {
        const f32 offset[2] = {m_offset.x, m_offset.y};
        out.SetFloatArray("offset", offset, 2);
    }
    return true;
}

void ColliderComponent::OnAttach() {
    CollisionSystem::Register(*this);
}

void ColliderComponent::OnDetach() {
    CollisionSystem::Unregister(*this);
}

// ---------------------------------------------------------------------------
//  AABBColliderComponent
// ---------------------------------------------------------------------------

StringId AABBColliderComponent::TypeIdStatic() {
    static const StringId id = Intern(kTypeName);
    return id;
}

bool AABBColliderComponent::Deserialize(const ConfigNode& node, std::string& outError) {
    if (!ColliderComponent::Deserialize(node, outError)) {
        return false;
    }
    if (const ConfigNode half = node.Child("halfExtents"); half.IsValid()) {
        f32 xy[2] = {0.5f, 0.5f};
        if (!half.AsFloatArray(xy, 2)) {
            outError = half.Path() + " must be an array of two numbers";
            return false;
        }
        m_halfExtents = Vec2{xy[0], xy[1]};
    }
    return true;
}

bool AABBColliderComponent::Serialize(ConfigWriter& out) const {
    if (!ColliderComponent::Serialize(out)) {
        return false;
    }
    const f32 half[2] = {m_halfExtents.x, m_halfExtents.y};
    out.SetFloatArray("halfExtents", half, 2);
    return true;
}

AABB AABBColliderComponent::WorldBounds() const {
    const Transform2D* transform = OwnerTransform();
    if (transform == nullptr) {
        return AABB{m_offset, m_offset};
    }

    // THE AXIS-ALIGNED BOUNDS OF THE ROTATED BOX. Four corners through the
    // world matrix, then the box that contains them. Over-approximates under
    // rotation, which fires collisions slightly EARLY - the safe direction.
    // See the header.
    const Mat3 world = transform->WorldMatrix();
    const Vec2 corners[4] = {
        world.TransformPoint(m_offset + Vec2{-m_halfExtents.x, -m_halfExtents.y}),
        world.TransformPoint(m_offset + Vec2{ m_halfExtents.x, -m_halfExtents.y}),
        world.TransformPoint(m_offset + Vec2{ m_halfExtents.x,  m_halfExtents.y}),
        world.TransformPoint(m_offset + Vec2{-m_halfExtents.x,  m_halfExtents.y}),
    };

    AABB bounds{corners[0], corners[0]};
    for (int i = 1; i < 4; ++i) {
        bounds.Encapsulate(corners[i]);
    }
    return bounds;
}

// ---------------------------------------------------------------------------
//  CircleColliderComponent
// ---------------------------------------------------------------------------

StringId CircleColliderComponent::TypeIdStatic() {
    static const StringId id = Intern(kTypeName);
    return id;
}

bool CircleColliderComponent::Deserialize(const ConfigNode& node, std::string& outError) {
    if (!ColliderComponent::Deserialize(node, outError)) {
        return false;
    }
    m_radius = static_cast<f32>(node.Child("radius").AsFloat(m_radius));
    if (m_radius < 0.0f) {
        outError = node.Path() + ".radius must not be negative";
        return false;
    }
    return true;
}

bool CircleColliderComponent::Serialize(ConfigWriter& out) const {
    if (!ColliderComponent::Serialize(out)) {
        return false;
    }
    out.SetFloat("radius", static_cast<f64>(m_radius));
    return true;
}

Circle CircleColliderComponent::WorldCircle() const {
    const Transform2D* transform = OwnerTransform();
    if (transform == nullptr) {
        return Circle{m_offset, m_radius};
    }
    const Mat3 world = transform->WorldMatrix();
    const Vec2 scale = world.GetScale();

    // A circle under a non-uniform scale is an ellipse, which this engine does
    // not have a test for. The LARGER axis is used, so the circle
    // over-approximates rather than under-approximates - the same safe
    // direction as the rotated box above, and stated for the same reason.
    const f32 worldRadius = m_radius * std::max(scale.x, scale.y);
    return Circle{world.TransformPoint(m_offset), worldRadius};
}

AABB CircleColliderComponent::WorldBounds() const {
    const Circle circle = WorldCircle();
    return AABB::FromCenterHalfExtents(circle.center, Vec2{circle.radius, circle.radius});
}

// ---------------------------------------------------------------------------
//  CollisionSystem
// ---------------------------------------------------------------------------

void CollisionSystem::Register(ColliderComponent& collider) {
    g_colliders.push_back(ColliderRecord{&collider, collider.OwnerHandle()});
}

void CollisionSystem::Unregister(ColliderComponent& collider) {
    const auto it = std::find_if(g_colliders.begin(), g_colliders.end(),
                                 [&collider](const ColliderRecord& record) {
                                     return record.collider == &collider;
                                 });
    if (it == g_colliders.end()) {
        return;
    }

    const EntityHandle owner = it->owner;

    // *** EXIT ON DESTRUCTION. *** Every pair this collider was in produces an
    // exit for the SURVIVING entity, sent before the pair disappears from the
    // set. Without this, a door opened on enter never closes when the key that
    // opened it is destroyed inside the volume - see the header.
    for (auto pairIt = g_lastFrame.begin(); pairIt != g_lastFrame.end();) {
        if (pairIt->first == owner.value || pairIt->second == owner.value) {
            const u32 survivorValue =
                (pairIt->first == owner.value) ? pairIt->second : pairIt->first;
            EntityHandle survivor;
            survivor.value = survivorValue;
            SendPairEvent(MessageTypes::CollisionExit(), survivor, owner);
            pairIt = g_lastFrame.erase(pairIt);
        } else {
            ++pairIt;
        }
    }

    // Swap and pop. Order does not matter here - unlike the sprite records,
    // nothing caches an index into this array, because the collider itself is
    // found by pointer.
    *it = g_colliders.back();
    g_colliders.pop_back();
}

void CollisionSystem::Clear() {
    g_colliders.clear();
    g_lastFrame.clear();
    g_thisFrame.clear();
}

void CollisionSystem::Update(f32 /*deltaSeconds*/) {
    // The scoped timer comes from SystemScheduler, which wraps every system's
    // Update in one named after the system - which is what puts collision on
    // the profiler HUD as its own line item.
    Scene* scene = Scene::Active();
    if (scene == nullptr) {
        return;
    }

    g_thisFrame.clear();
    g_pairTests = 0;

    // O(n^2), every pair against every other pair, and that is fine. See the
    // broad-phase note in the header: the decision to build a grid gets made
    // from the number this timer produces, not from intuition.
    for (usize i = 0; i < g_colliders.size(); ++i) {
        ColliderComponent* a = g_colliders[i].collider;
        const EntityHandle ha = g_colliders[i].owner;

        // DECISION 1 from DeferredOps: an entity destroyed this frame still
        // updates and still renders, but does NOT collide. "Destroyed but
        // still colliding" produces an enemy that goes on damaging the player
        // after it visibly died.
        if (a == nullptr || DeferredOps::IsPendingDestroy(ha) || !scene->IsValid(ha)) {
            continue;
        }

        for (usize j = i + 1; j < g_colliders.size(); ++j) {
            ColliderComponent* b = g_colliders[j].collider;
            const EntityHandle hb = g_colliders[j].owner;

            if (b == nullptr || DeferredOps::IsPendingDestroy(hb) || !scene->IsValid(hb)) {
                continue;
            }
            if (!LayersInterested(*a, *b)) {
                continue;   // both masks must match - the real performance win
            }

            ++g_pairTests;
            if (ShapesOverlap(*a, *b)) {
                g_thisFrame.insert(MakePair(ha, hb));
            }
        }
    }

    // THE DIFF. Both entities receive every event, each naming the other as
    // the partner - which is the Week 10 verification "two entities collide;
    // BOTH receive an event with the correct partner".
    for (const Pair& pair : g_thisFrame) {
        EntityHandle a; a.value = pair.first;
        EntityHandle b; b.value = pair.second;

        const bool wasOverlapping = g_lastFrame.contains(pair);
        const StringId type = wasOverlapping ? MessageTypes::CollisionStay()
                                             : MessageTypes::CollisionEnter();
        if (!wasOverlapping) {
            ++g_enterEvents;
        }
        SendPairEvent(type, a, b);
        SendPairEvent(type, b, a);
    }

    for (const Pair& pair : g_lastFrame) {
        if (g_thisFrame.contains(pair)) {
            continue;
        }
        // EXIT. The one that gets forgotten, and the one gameplay depends on
        // most. Note that the pair is removed from the tracking set AFTER the
        // event is generated - removing first is why exits sometimes never
        // fire at all.
        EntityHandle a; a.value = pair.first;
        EntityHandle b; b.value = pair.second;
        SendPairEvent(MessageTypes::CollisionExit(), a, b);
        SendPairEvent(MessageTypes::CollisionExit(), b, a);
    }

    g_lastFrame = g_thisFrame;

    // Debug draw every collider, through the Week 6 debug draw, on the
    // Colliders category so the editor's Debug Draw panel can switch it off.
    if (g_drawColliders == nullptr) {
        g_drawColliders = CVarRegistry::Find("debug.drawColliders");
    }
    if (g_drawColliders != nullptr && g_drawColliders->GetBool()) {
        for (const ColliderRecord& record : g_colliders) {
            if (record.collider == nullptr) {
                continue;
            }
            const bool overlapping =
                std::any_of(g_thisFrame.begin(), g_thisFrame.end(), [&](const Pair& pair) {
                    return pair.first == record.owner.value || pair.second == record.owner.value;
                });
            const Color color = record.collider->IsTrigger()
                                    ? Color::Cyan()
                                    : (overlapping ? Color::Red() : Color::Green());

            if (record.collider->Shape() == ColliderShape::Circle) {
                const Circle circle =
                    static_cast<const CircleColliderComponent*>(record.collider)->WorldCircle();
                DebugDraw::Circle(circle.center, circle.radius, color, 0.0f,
                                  DebugSpace::World, DebugCategory::Colliders);
            } else {
                DebugDraw::Box(record.collider->WorldBounds(), color, 0.0f,
                               DebugSpace::World, DebugCategory::Colliders);
            }
        }
    }
}

usize CollisionSystem::ColliderCount()    { return g_colliders.size(); }
usize CollisionSystem::ActivePairCount()  { return g_lastFrame.size(); }
u64   CollisionSystem::PairTestsLastFrame() { return g_pairTests; }
u64   CollisionSystem::TotalEnterEvents() { return g_enterEvents; }

void CollisionSystem::RegisterComponentTypes() {
    ComponentFactory::Register(AABBColliderComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<AABBColliderComponent>();
                               });
    ComponentFactory::Register(CircleColliderComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<CircleColliderComponent>();
                               });
}

} // namespace eng
