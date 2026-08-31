// ============================================================================
//  Collider.cpp - collider components and the collision system. See Collider.h.
//
//  Notice that this file CALLS Overlaps() and never implements it. All of the
//  actual geometry lives in math/Overlap.h as plain functions.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/physics/Collider.h>
#include <engine/render/Gizmos.h>
#include <engine/scene/DeferredOps.h>
#include <engine/scene/Messaging.h>
#include <engine/scene/Scene.h>

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

namespace eng {
namespace {

// One entry per collider currently attached to something.
struct ColliderRecord {
    ColliderComponent* collider = nullptr;
    EntityId           owner{};
};

std::vector<ColliderRecord> g_colliders;

// A pair of entities that are touching. Always stored with the smaller id
// first, so that (a,b) and (b,a) are recognised as the same pair. Forgetting
// that is how you end up with an "enter" event every single frame - the set
// never matches, because the order flipped.
using Pair = std::pair<EntityId, EntityId>;

std::set<Pair> g_lastFrame;
std::set<Pair> g_thisFrame;

Pair MakePair(EntityId a, EntityId b) {
    return (a < b) ? Pair{a, b} : Pair{b, a};
}

// BOTH sides have to be interested. See the note in Collider.h.
bool LayersInterested(const ColliderComponent& a, const ColliderComponent& b) {
    return a.CaresAbout(b.Layer()) && b.CaresAbout(a.Layer());
}

bool ShapesOverlap(const ColliderComponent& a, const ColliderComponent& b) {
    const bool aIsCircle = a.Shape() == ColliderShape::Circle;
    const bool bIsCircle = b.Shape() == ColliderShape::Circle;

    // Every branch here is one call into math/Overlap.h.
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

void SendPairEvent(const char* type, EntityId to, EntityId partner) {
    Message message;
    message.type   = type;
    message.target = to;
    message.sender = partner;
    message.other  = partner;
    MessageBus::Send(message);
}

} // namespace

// ---------------------------------------------------------------------------
//  ColliderComponent - the parts every collider shares
// ---------------------------------------------------------------------------

bool ColliderComponent::CaresAbout(const std::string& layer) const {
    for (const std::string& wanted : m_collidesWith) {
        if (wanted == kCollisionLayerAll || wanted == layer) {
            return true;
        }
    }
    return false;
}

bool ColliderComponent::Deserialize(const Json& node, std::string& outError) {
    m_layer = ReadString(node, "layer", "Default", "Collider");

    // "collidesWith" may be a single name or a list of them, because both read
    // naturally in a file and supporting both costs four lines.
    if (HasKey(node, "collidesWith")) {
        const Json& wanted = node["collidesWith"];
        m_collidesWith.clear();

        if (wanted.is_string()) {
            m_collidesWith.push_back(wanted.get<std::string>());
        } else if (wanted.is_array()) {
            for (const Json& item : wanted) {
                if (item.is_string()) {
                    m_collidesWith.push_back(item.get<std::string>());
                }
            }
        } else {
            outError = "\"collidesWith\" should be a layer name or a list of them";
            return false;
        }

        if (m_collidesWith.empty()) {
            // An empty list means this collider is interested in nothing at
            // all, which is legal but almost certainly a mistake.
            ENGINE_LOG_WARN(Channels::kPhysics,
                            "a collider on layer '{}' collides with nothing, so it will "
                            "never report anything", m_layer);
        }
    }

    m_trigger = ReadBool(node, "trigger", false, "Collider");
    m_offset  = ReadVec2(node, "offset", Vec2{0.0f, 0.0f}, "Collider");

    outError.clear();
    return true;
}

bool ColliderComponent::Serialize(Json& out) const {
    out["layer"] = m_layer;

    Json wanted = Json::array();
    for (const std::string& layer : m_collidesWith) {
        wanted.push_back(layer);
    }
    out["collidesWith"] = std::move(wanted);

    out["trigger"] = m_trigger;

    // Only written when it is not zero, so ordinary colliders do not carry a
    // meaningless "offset": [0, 0] around in every scene file.
    if (m_offset.x != 0.0f || m_offset.y != 0.0f) {
        WriteVec2(out, "offset", m_offset);
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
//  AABBColliderComponent - a rectangle
// ---------------------------------------------------------------------------

bool AABBColliderComponent::Deserialize(const Json& node, std::string& outError) {
    if (!ColliderComponent::Deserialize(node, outError)) {
        return false;
    }
    m_halfExtents = ReadVec2(node, "halfExtents", m_halfExtents, kTypeName);
    return true;
}

bool AABBColliderComponent::Serialize(Json& out) const {
    if (!ColliderComponent::Serialize(out)) {
        return false;
    }
    WriteVec2(out, "halfExtents", m_halfExtents);
    return true;
}

AABB AABBColliderComponent::WorldBounds() const {
    const Transform2D* transform = OwnerTransform();
    if (transform == nullptr) {
        return AABB{m_offset, m_offset};
    }

    // Push all four corners through the world transform and take the upright
    // box that contains them. That is simplification 1 from the header: if the
    // entity is rotated, this box is bigger than the real shape, so collisions
    // fire slightly early rather than slightly late.
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

bool CircleColliderComponent::Deserialize(const Json& node, std::string& outError) {
    if (!ColliderComponent::Deserialize(node, outError)) {
        return false;
    }
    m_radius = ReadFloat(node, "radius", m_radius, kTypeName);
    if (m_radius < 0.0f) {
        outError = "a circle collider's \"radius\" cannot be negative";
        return false;
    }
    return true;
}

bool CircleColliderComponent::Serialize(Json& out) const {
    if (!ColliderComponent::Serialize(out)) {
        return false;
    }
    out["radius"] = m_radius;
    return true;
}

Circle CircleColliderComponent::WorldCircle() const {
    const Transform2D* transform = OwnerTransform();
    if (transform == nullptr) {
        return Circle{m_offset, m_radius};
    }
    const Mat3 world = transform->WorldMatrix();
    const Vec2 scale = world.GetScale();

    // A circle stretched more on one axis than the other is an oval, and this
    // engine has no oval test. The LARGER of the two scales is used, so the
    // circle comes out slightly too big rather than slightly too small - the
    // same safe direction as the rotated box above.
    const float worldRadius = m_radius * std::max(scale.x, scale.y);
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
    g_colliders.push_back(ColliderRecord{&collider, collider.OwnerId()});
}

void CollisionSystem::Unregister(ColliderComponent& collider) {
    const auto it = std::find_if(g_colliders.begin(), g_colliders.end(),
                                 [&collider](const ColliderRecord& record) {
                                     return record.collider == &collider;
                                 });
    if (it == g_colliders.end()) {
        return;
    }

    const EntityId owner = it->owner;

    // EXIT WHEN ONE SIDE IS DESTROYED. Every pair this collider was part of
    // sends an exit to the OTHER entity, now, before the pair disappears.
    // Without this a door opened on enter never closes when the key that
    // opened it is destroyed inside the doorway.
    for (auto pairIt = g_lastFrame.begin(); pairIt != g_lastFrame.end();) {
        if (pairIt->first == owner || pairIt->second == owner) {
            const EntityId survivor = (pairIt->first == owner) ? pairIt->second
                                                               : pairIt->first;
            SendPairEvent(MessageTypes::kCollisionExit, survivor, owner);

            // erase() returns an iterator to the next element, which is what
            // makes it safe to remove entries while walking the set.
            pairIt = g_lastFrame.erase(pairIt);
        } else {
            ++pairIt;
        }
    }

    // Swap and pop, as with sprites. Nothing remembers a position in this list
    // - a collider is found by its pointer - so no fix-up is needed here.
    *it = g_colliders.back();
    g_colliders.pop_back();
}

void CollisionSystem::Clear() {
    g_colliders.clear();
    g_lastFrame.clear();
    g_thisFrame.clear();
}

void CollisionSystem::Update(float /*deltaSeconds*/) {
    Scene* scene = Scene::Active();
    if (scene == nullptr) {
        return;
    }

    g_thisFrame.clear();

    // Every collider against every other collider. The j = i + 1 is what stops
    // each pair being tested twice and stops anything being tested against
    // itself.
    for (std::size_t i = 0; i < g_colliders.size(); ++i) {
        ColliderComponent* a  = g_colliders[i].collider;
        const EntityId     ha = g_colliders[i].owner;

        // Something queued for destruction this tick still draws and still
        // updates, but does NOT collide - see rule 1 in DeferredOps.h.
        // "Destroyed but still hurting you" is a genuinely confusing bug.
        if (a == nullptr || DeferredOps::IsPendingDestroy(ha) || !scene->IsValid(ha)) {
            continue;
        }

        for (std::size_t j = i + 1; j < g_colliders.size(); ++j) {
            ColliderComponent* b  = g_colliders[j].collider;
            const EntityId     hb = g_colliders[j].owner;

            if (b == nullptr || DeferredOps::IsPendingDestroy(hb) || !scene->IsValid(hb)) {
                continue;
            }
            if (!LayersInterested(*a, *b)) {
                continue;   // checked before the shape test, because it is cheaper
            }
            if (ShapesOverlap(*a, *b)) {
                g_thisFrame.insert(MakePair(ha, hb));
            }
        }
    }

    // Compare this tick's pairs against last tick's to produce the events.
    // BOTH entities get every event, each naming the other as the partner.
    for (const Pair& pair : g_thisFrame) {
        const bool  wasTouching = g_lastFrame.contains(pair);
        const char* type = wasTouching ? MessageTypes::kCollisionStay
                                       : MessageTypes::kCollisionEnter;
        SendPairEvent(type, pair.first, pair.second);
        SendPairEvent(type, pair.second, pair.first);
    }

    for (const Pair& pair : g_lastFrame) {
        if (g_thisFrame.contains(pair)) {
            continue;
        }
        // Exit: it was touching last tick and is not now.
        SendPairEvent(MessageTypes::kCollisionExit, pair.first, pair.second);
        SendPairEvent(MessageTypes::kCollisionExit, pair.second, pair.first);
    }

    g_lastFrame = g_thisFrame;

    // Draw every collider as a gizmo, so you can SEE what the collision system
    // thinks the shapes are. Green is not touching anything, red is touching
    // something, cyan is a trigger. The Scene view's Gizmos menu switches the
    // whole Colliders category off.
    if (Gizmos::IsCategoryEnabled(GizmoCategory::Colliders)) {
        for (const ColliderRecord& record : g_colliders) {
            if (record.collider == nullptr) {
                continue;
            }
            const bool touching =
                std::any_of(g_thisFrame.begin(), g_thisFrame.end(),
                            [&](const Pair& pair) {
                                return pair.first == record.owner ||
                                       pair.second == record.owner;
                            });

            const Color color = record.collider->IsTrigger()
                                    ? Color::Cyan()
                                    : (touching ? Color::Red() : Color::Green());

            if (record.collider->Shape() == ColliderShape::Circle) {
                const Circle circle =
                    static_cast<const CircleColliderComponent*>(record.collider)
                        ->WorldCircle();
                Gizmos::Circle(circle.center, circle.radius, color, 0.0f,
                               GizmoSpace::World, GizmoCategory::Colliders);
            } else {
                Gizmos::Box(record.collider->WorldBounds(), color, 0.0f,
                            GizmoSpace::World, GizmoCategory::Colliders);
            }
        }
    }
}

std::size_t CollisionSystem::ColliderCount()   { return g_colliders.size(); }
std::size_t CollisionSystem::ActivePairCount() { return g_lastFrame.size(); }

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
