// ============================================================================
//  Collider.cpp - collider components and the collision system. See Collider.h.
//
//  Notice what this file never does: geometry. Every "are these touching"
//  question is one call to Collide() in math/Overlap.h, and every collider's
//  job here is only to turn its own description plus a world matrix into
//  ConvexShapes. That split is what let five shapes replace two without this
//  file growing a table of special cases.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/physics/Collider.h>
#include <engine/physics/Physics2D.h>
#include <engine/physics/Rigidbody.h>
#include <engine/render/Gizmos.h>
#include <engine/scene/DeferredOps.h>
#include <engine/scene/Entity.h>
#include <engine/scene/Messaging.h>
#include <engine/scene/Scene.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eng {
namespace {

// ---------------------------------------------------------------------------
//  The collider list, and the shapes for this step
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
//  EVERY COLLIDER GETS A NUMBER, and a contact is keyed on a pair of THOSE
//  rather than on a pair of entities.
//
//  WHAT THAT FIXES, because it reads like bookkeeping and is not. Pairs used
//  to be keyed by ENTITY while the loop below tests COLLIDERS, and the store
//  was a std::map::emplace, which does not overwrite. So an entity with two
//  colliders touching one partner kept ONE contact - whichever the broad
//  phase reached first - and that single record decided the fate of the whole
//  entity pair:
//
//    the TRIGGER one survived  solid() below is false, so the solid contact
//                              is never solved and THE OTHER ENTITY PASSES
//                              STRAIGHT THROUGH A SOLID COLLIDER
//    the SOLID one survived    the trigger never fires at all
//
//  Which one survived was decided by the sort on bounds.min.x below, so
//  moving a collider one pixel could change it, and moving an object could
//  change it mid-overlap - sending an enter of one kind and an exit of the
//  other. Collider.h's own advice for a compound body - put every collider on
//  the entity that has the rigidbody rather than on children - walks straight
//  into it.
//
//  Unity sends one callback per COLLIDER pair. So does this, now.
//
//  A NUMBER AND NOT A ColliderComponent*, for one reason that is easy to miss:
//  std::map iteration order IS the solver's sweep order below, and a pointer
//  key orders by heap address - stable within a run, different between runs.
//  Ids are handed out in Register order, which is attach order, which is the
//  order the scene file lists components in, so the solver sweeps identically
//  on every run and machine. A physics bug stays bisectable. It is also why
//  an id is never reused and why Clear() does not reset the counter.
// ---------------------------------------------------------------------------
using ColliderId = std::uint32_t;   // 0 means "no collider"

ColliderId g_nextColliderId = 1;

struct ColliderRecord {
    ColliderComponent* collider = nullptr;
    EntityId           owner{};
    ColliderId         id = 0;
};

std::vector<ColliderRecord> g_colliders;

// A pair of COLLIDERS that are touching. Always stored with the smaller id
// first, so that (a,b) and (b,a) are recognised as the same pair. Forgetting
// that is how you end up with an "enter" event every single frame - the set
// never matches, because the order flipped.
using CPair = std::pair<ColliderId, ColliderId>;

CPair MakePair(ColliderId a, ColliderId b) {
    return (a < b) ? CPair{a, b} : CPair{b, a};
}

// A pair of ENTITIES, which is still what an event names and still what
// FindContact is asked about. Only the index below is keyed on it.
using EPair = std::pair<EntityId, EntityId>;

EPair MakeEntityPair(EntityId a, EntityId b) {
    return (a < b) ? EPair{a, b} : EPair{b, a};
}

// What a touching pair needs that its key cannot carry: whose colliders these
// are, and whether the pair reports as a trigger. `a` is always the owner of
// the pair's FIRST collider.
//
// THE TRIGGER FLAG LIVES HERE rather than in a second container beside this
// one, which is what it used to be. One container cannot drift out of step
// with itself on an erase - and the exit loop and Unregister can both read the
// kind without dereferencing a collider that may already have been destroyed,
// which is the whole reason the kind has to be remembered at all: a pickup
// destroyed while overlapping would otherwise send a trigger-enter and a
// collision-exit, and a script listening for only one would never see the
// pair close.
struct PairInfo {
    EntityId a{};
    EntityId b{};
    bool     isTrigger = false;
};

std::map<CPair, PairInfo> g_lastFrame;
std::map<CPair, PairInfo> g_thisFrame;

// The contact for every touching collider pair this step, so that the script
// dispatcher can attach it to the event. Keyed the same way as the pair maps.
std::map<CPair, Collision2D> g_contacts;

// Which collider pair holds the DEEPEST contact between two entities - one
// index for solid pairs and one for trigger pairs.
//
// THE ONE THING STILL ASKED FOR BY ENTITY. An event names two entities, not
// two colliders, so the dispatcher can only ask by entity - and a compound
// body may hold several contacts with one partner, so the answer has to be
// one of them. The deepest, by the same tie-break the sub-shape loop uses.
//
// SPLIT BY KIND, AND THAT IS THE POINT OF HAVING TWO. A collision hook must
// never be handed a trigger's record nor the other way round, and with one
// shared slot per entity pair it could be: before this existed, whichever
// kind won the race above decided what BOTH families were told. Each lookup
// now answers for its own kind or answers nothing.
std::map<EPair, CPair> g_entityContact;   // solid pairs
std::map<EPair, CPair> g_entityTrigger;   // trigger pairs

// Shared by FindContact and FindTriggerContact, which differ only in which
// index they consult.
const Collision2D* LookUpByEntities(const std::map<EPair, CPair>& index, EntityId a,
                                    EntityId b) {
    const auto it = index.find(MakeEntityPair(a, b));
    if (it == index.end()) {
        return nullptr;
    }
    const auto contact = g_contacts.find(it->second);
    return (contact == g_contacts.end()) ? nullptr : &contact->second;
}

bool g_collisionsSubscribed = false;

int         g_solverIterations = 4;

// ===========================================================================
//  THE CLOSING SPEED BELOW WHICH A BOUNCE IS NOT WORTH HAVING.
//
//  Without one, any bounciness above zero means a body NEVER COMES TO REST.
//  Every step gravity adds a sliver of closing speed, the contact hands a
//  fraction of it back, and the body buzzes against the floor forever with a
//  velocity that never reaches zero - so "is it standing still?" has no
//  answer, and a script waiting for one waits all game.
//
//  WORKED OUT FROM GRAVITY AND THE STEP rather than written down, because the
//  right number is "a couple of steps' worth of falling" and this engine does
//  not fix its units. At the shipped default of 981 px/s^2 and a sixtieth of
//  a second that is about 33 px/s; a project working in metres gets 0.33 m/s
//  instead of a threshold three hundred times too big. Box2D writes a
//  constant here and can afford to, because Box2D fixes the unit as the metre.
//
//  Set at the top of each Update, so changing gravity or the step takes
//  effect at once.
// ===========================================================================
float g_restingSpeed = 0.0f;
std::size_t g_narrowPhaseTests = 0;

// How many pairs the SWEEP actually looked at - every inner-loop iteration it
// reached before breaking out. Counted rather than derived, because the whole
// claim of the broad phase is that this number is far below n(n-1)/2, and a
// derived figure could not show that.
std::size_t g_broadPhasePairs = 0;
std::size_t g_bruteForcePairs  = 0;

// Every collider's world shapes for this step, in one flat array. A collider
// owns the slice [start, start + count).
//
// FLAT RATHER THAN A FIXED BLOCK PER COLLIDER. An edge collider may produce 32
// shapes and a circle produces 1, so giving every collider room for 32 would
// waste about 2 KB each for the overwhelmingly common case.
// WHERE THE POSITION SOLVER IS MOVING EACH BODY TO, accumulated across its
// passes and applied once at the end. Nothing moves while the sweep is part
// way through its own list, which is what lets a pass read the separations
// the previous pass produced without the shapes having to be rebuilt.
//
// Kept at file scope rather than built per step so that a scene with a
// hundred contacts asks the system for memory once.
std::unordered_map<Rigidbody2DComponent*, Vec2> g_corrections;

std::vector<ConvexShape> g_shapePool;

struct ShapeSlice {
    int  start = 0;
    int  count = 0;
    AABB bounds{};
};
std::vector<ShapeSlice> g_slices;

// BOTH sides have to be interested. See the note in Collider.h.
bool LayersInterested(const ColliderComponent& a, const ColliderComponent& b) {
    return a.CaresAbout(b.Layer()) && b.CaresAbout(a.Layer());
}

// ---------------------------------------------------------------------------
//  Winding
//
//  Overlap.h's edge normals assume the points go round anticlockwise. A world
//  matrix with a NEGATIVE determinant - any odd number of mirrored axes, which
//  is what a scale of -1 on x is - reverses that, and the shape would then
//  collide inside-out.
//
//  Cheaper to detect than to forbid: the signed area is negative exactly when
//  the winding is backwards, and reversing a list of at most eight points
//  costs nothing.
// ---------------------------------------------------------------------------
void MakeAnticlockwise(Vec2* points, int count) {
    if (count < 3) {
        return;   // a point or a segment has no winding
    }
    float twiceArea = 0.0f;
    for (int i = 0; i < count; ++i) {
        twiceArea += Cross(points[i], points[(i + 1) % count]);
    }
    if (twiceArea < 0.0f) {
        std::reverse(points, points + count);
    }
}

// The shoelace formula: twice the signed area of a polygon. Used for the area
// a density is multiplied by, and for the winding test above.
float PolygonArea(const std::vector<Vec2>& points) {
    if (points.size() < 3) {
        return 0.0f;
    }
    float twiceArea = 0.0f;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Vec2 current = points[i];
        const Vec2 next    = points[(i + 1) % points.size()];
        twiceArea += Cross(current, next);
    }
    return std::fabs(twiceArea) * 0.5f;
}

// Reading a list of points out of a scene file.
bool ReadPointList(const Json& node, std::string_view key, std::vector<Vec2>& out,
                   std::string& outError) {
    if (!HasKey(node, key)) {
        return true;   // leave the default alone
    }
    const Json& array = node[std::string(key)];
    if (!array.is_array()) {
        outError = std::string("\"") + std::string(key) +
                   "\" should be a list of [x, y] points";
        return false;
    }

    out.clear();
    for (const Json& item : array) {
        if (!item.is_array() || item.size() < 2) {
            outError = std::string("every point in \"") + std::string(key) +
                       "\" should be a pair like [12, 34]";
            return false;
        }
        out.push_back(Vec2{item[0].get<float>(), item[1].get<float>()});
    }
    return true;
}

void WritePointList(Json& out, std::string_view key, const std::vector<Vec2>& points) {
    Json array = Json::array();
    for (const Vec2& p : points) {
        array.push_back(Json::array({p.x, p.y}));
    }
    out[std::string(key)] = std::move(array);
}

// ---------------------------------------------------------------------------
//  WHICH COLLIDER PAIR EACH EVENT WAS SENT FOR, in the order they were sent.
//
//  A message carries entity ids and a small payload - see Messaging.h, and
//  the argument there for keeping it small. A CPair is two collider ids and
//  does not fit in the one int it has, so the int carries a 1-BASED TOKEN
//  into this list instead and the collision system hands back the contact.
//
//  THIS IS WHAT MAKES "EVENTS ARE PER COLLIDER PAIR" TRUE. The dispatcher
//  used to re-derive the contact from an entity-keyed index, which holds one
//  CPair per entity pair: the deepest. A wall with two colliders therefore
//  sent two OnCollisionEnter calls whose contacts were byte-identical, both
//  naming the same collider, with the shallower pair's contact unreachable
//  from any API at all.
//
//  Cleared at the top of every step, and a token is only meaningful until the
//  next one - which is the same lifetime the contacts themselves have.
// ---------------------------------------------------------------------------
std::vector<CPair> g_eventPairs;

// A token of 0 means "no pair", which is what an exit sends: an exit has no
// contact to describe, by definition and by design. See Contact.h.
void SendPairEvent(const char* type, EntityId to, EntityId partner, int pairToken = 0) {
    Message message;
    message.type   = type;
    message.target = to;
    message.sender = partner;
    message.other  = partner;
    message.i0     = pairToken;
    MessageBus::Send(message);
}

// Remembers a pair and returns the token for it. Both directions of one event
// share a token: the stored contact is oriented for whichever collider sorted
// first, and the dispatcher turns it round for the other receiver.
int TokenFor(const CPair& pair) {
    g_eventPairs.push_back(pair);
    return static_cast<int>(g_eventPairs.size());
}

// ---------------------------------------------------------------------------
//  Drawing any shape, so the gizmo code is not five near-copies
// ---------------------------------------------------------------------------
void DrawShapeGizmo(const ConvexShape& shape, Color color) {
    constexpr GizmoSpace    kSpace    = GizmoSpace::World;
    constexpr GizmoCategory kCategory = GizmoCategory::Colliders;

    if (shape.count == 1) {
        Gizmos::Circle(shape.points[0], shape.radius, color, 0.0f, kSpace, kCategory);
        return;
    }

    const bool closed = shape.count > 2;
    const int  edges  = closed ? shape.count : 1;

    if (shape.radius <= 0.0f) {
        for (int i = 0; i < edges; ++i) {
            Gizmos::Line(shape.points[i], shape.points[(i + 1) % shape.count], color,
                         0.0f, kSpace, kCategory);
        }
        return;
    }

    // Rounded: a circle at every corner plus the sides pushed outward by the
    // radius. For a two-point shape that draws exactly a capsule, and for a
    // rounded box it draws the right outline too.
    for (int i = 0; i < shape.count; ++i) {
        Gizmos::Circle(shape.points[i], shape.radius, color, 0.0f, kSpace, kCategory);
    }
    for (int i = 0; i < edges; ++i) {
        const Vec2 from = shape.points[i];
        const Vec2 to   = shape.points[(i + 1) % shape.count];
        const Vec2 out  = (to - from).Perpendicular().Normalized() * shape.radius;
        Gizmos::Line(from + out, to + out, color, 0.0f, kSpace, kCategory);
        Gizmos::Line(from - out, to - out, color, 0.0f, kSpace, kCategory);
    }
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

Mat3 ColliderComponent::OwnerWorldMatrix() const {
    const Transform2D* transform = OwnerTransform();
    return transform != nullptr ? transform->WorldMatrix() : Mat3::Identity();
}

Vec2 ColliderComponent::OwnerWorldScale() const {
    const Vec2 scale = OwnerWorldMatrix().GetScale();

    // ABSOLUTE, because a mirrored entity - scale (-1, 1), which is how a
    // sprite is flipped - is the same SIZE as an unmirrored one. A negative
    // factor here would give a shape a negative area and a body a negative
    // mass, and the solver divides by mass.
    return Vec2{std::fabs(scale.x), std::fabs(scale.y)};
}

void ColliderComponent::SetDensity(float density) {
    // Zero density would make an auto-mass body weigh nothing, and the solver
    // divides by mass.
    m_density = std::max(density, 0.0001f);
}

int GatherWorldShapes(const ColliderComponent& collider,
                      std::vector<ConvexShape>& into) {
    const int capacity = std::max(collider.WorldShapeCount(), 1);
    into.resize(static_cast<std::size_t>(capacity));

    // Shrunk back to what was actually written. A collider may report a
    // capacity and then write fewer - an edge collider with one point does -
    // and leaving the tail in place would hand out default-constructed shapes
    // that overlap nothing and quietly break nothing, which is the worst way
    // for a bug to behave.
    const int count = collider.WorldShapes(into.data(), capacity);
    into.resize(static_cast<std::size_t>(std::max(count, 0)));
    return static_cast<int>(into.size());
}

AABB ColliderComponent::WorldBounds() const {
    // A scratch buffer kept between calls rather than built on each one. This
    // is asked for every collider every step, and the engine's simulation is
    // single-threaded with nothing here re-entering, so one buffer does.
    static std::vector<ConvexShape> shapes;
    const int                       count = GatherWorldShapes(*this, shapes);
    if (count <= 0) {
        const Vec2 at = OwnerWorldMatrix().TransformPoint(m_offset);
        return AABB{at, at};
    }

    AABB bounds = shapes[0].Bounds();
    for (int i = 1; i < count; ++i) {
        const AABB other = shapes[i].Bounds();
        bounds.Encapsulate(other.min);
        bounds.Encapsulate(other.max);
    }
    return bounds;
}

Rigidbody2DComponent* ColliderComponent::AttachedRigidbody() const {
    Entity* owner = Owner();
    return owner != nullptr ? owner->Find<Rigidbody2DComponent>() : nullptr;
}

Vec2 ColliderComponent::ClosestPoint(Vec2 worldPoint) const {
    static std::vector<ConvexShape> shapes;
    const int                       count = GatherWorldShapes(*this, shapes);
    if (count <= 0) {
        return worldPoint;
    }

    Vec2  best       = eng::ClosestPoint(shapes[0], worldPoint);
    float bestDistSq = DistanceSquared(best, worldPoint);
    for (int i = 1; i < count; ++i) {
        const Vec2  candidate = eng::ClosestPoint(shapes[i], worldPoint);
        const float distSq    = DistanceSquared(candidate, worldPoint);
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            best       = candidate;
        }
    }
    return best;
}

bool ColliderComponent::OverlapPoint(Vec2 worldPoint) const {
    static std::vector<ConvexShape> shapes;
    const int                       count = GatherWorldShapes(*this, shapes);
    for (int i = 0; i < count; ++i) {
        if (Contains(shapes[i], worldPoint)) {
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
    m_enabled = ReadBool(node, "enabled", true, "Collider");

    m_material.friction = std::max(ReadFloat(node, "friction", 0.4f, "Collider"), 0.0f);

    // Clamped at 1. Above it a bounce returns MORE energy than it received and
    // the object climbs higher every time, forever - which reads as a bug in
    // the engine rather than as a number somebody typed.
    m_material.bounciness =
        std::clamp(ReadFloat(node, "bounciness", 0.0f, "Collider"), 0.0f, 1.0f);

    SetDensity(ReadFloat(node, "density", 1.0f, "Collider"));

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

    // The rest are only written when they differ from the default, so an
    // ordinary collider does not carry six meaningless lines around in every
    // scene file. A scene is read by people.
    if (m_offset.x != 0.0f || m_offset.y != 0.0f) {
        WriteVec2(out, "offset", m_offset);
    }
    if (!m_enabled) {
        out["enabled"] = m_enabled;
    }
    if (m_material.friction != 0.4f) {
        out["friction"] = m_material.friction;
    }
    if (m_material.bounciness != 0.0f) {
        out["bounciness"] = m_material.bounciness;
    }
    if (m_density != 1.0f) {
        out["density"] = m_density;
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
//  BoxColliderComponent
// ---------------------------------------------------------------------------

void BoxColliderComponent::SetEdgeRadius(float radius) {
    m_edgeRadius = std::max(radius, 0.0f);
}

bool BoxColliderComponent::Deserialize(const Json& node, std::string& outError) {
    if (!ColliderComponent::Deserialize(node, outError)) {
        return false;
    }

    // "size" is Unity's spelling and "halfExtents" is this engine's. Both are
    // accepted; half-extents wins if somebody writes both, because that is the
    // one this engine has always written and so the one an older file has.
    if (HasKey(node, "size") && !HasKey(node, "halfExtents")) {
        SetSize(ReadVec2(node, "size", Size(), kTypeName));
    } else {
        m_halfExtents = ReadVec2(node, "halfExtents", m_halfExtents, kTypeName);
    }

    SetEdgeRadius(ReadFloat(node, "edgeRadius", m_edgeRadius, kTypeName));
    return true;
}

bool BoxColliderComponent::Serialize(Json& out) const {
    if (!ColliderComponent::Serialize(out)) {
        return false;
    }
    WriteVec2(out, "halfExtents", m_halfExtents);
    if (m_edgeRadius != 0.0f) {
        out["edgeRadius"] = m_edgeRadius;
    }
    return true;
}

int BoxColliderComponent::WorldShapes(ConvexShape* out, int maxOut) const {
    if (maxOut < 1) {
        return 0;
    }

    // All four corners through the world matrix. Rotation, scale and the
    // parent chain are all already in that matrix, so there is nothing
    // special to do for any of them - which is exactly why a rotated box is
    // now a rotated box and not the upright box around it.
    const Mat3 world = OwnerWorldMatrix();
    Vec2       corners[4] = {
        world.TransformPoint(m_offset + Vec2{-m_halfExtents.x, -m_halfExtents.y}),
        world.TransformPoint(m_offset + Vec2{ m_halfExtents.x, -m_halfExtents.y}),
        world.TransformPoint(m_offset + Vec2{ m_halfExtents.x,  m_halfExtents.y}),
        world.TransformPoint(m_offset + Vec2{-m_halfExtents.x,  m_halfExtents.y}),
    };
    MakeAnticlockwise(corners, 4);

    const Vec2  scale       = world.GetScale();
    const float worldRadius = m_edgeRadius * std::max(scale.x, scale.y);

    out[0] = ToConvex(corners, 4, worldRadius);
    return 1;
}

float BoxColliderComponent::Area() const {
    const Vec2 scale = OwnerWorldScale();

    const float halfX = m_halfExtents.x * scale.x;
    const float halfY = m_halfExtents.y * scale.y;
    const float core  = 4.0f * halfX * halfY;

    // ======================================================================
    //  THE ROUNDING COUNTS, and leaving it out contradicted this function's
    //  own contract.
    //
    //  WorldShapes hands the solver the core inflated by edgeRadius, and
    //  Area() promises to be "the area of what WorldShapes() produces". A
    //  32x64 box with a radius of 16 is really 3876 units of shape and was
    //  reported as 1024 - so an auto-mass body was nearly four times too
    //  light for the thing being pushed, while LocalSize() beside it already
    //  counted the same rounding for the inertia. The two numbers describing
    //  one collider disagreed about how big it was.
    //
    //  Minkowski, exactly: the core, plus its perimeter swept by the radius,
    //  plus the disc the four quarter-corners add up to. The radius takes the
    //  LARGER scale, which is what WorldShapes gives it.
    // ======================================================================
    const float radius = m_edgeRadius * std::max(scale.x, scale.y);
    if (radius <= 0.0f) {
        return core;
    }
    return core + 4.0f * (halfX + halfY) * radius + kPi * radius * radius;
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
    const Mat3 world = OwnerWorldMatrix();
    const Vec2 scale = world.GetScale();

    // A circle stretched more on one axis than the other is an ellipse, and
    // this engine has no ellipse test. The LARGER of the two scales is used,
    // so the circle comes out slightly too big rather than slightly too small
    // - collisions then fire early, which is the safer direction for a game.
    // Use a capsule or a polygon when an ellipse is genuinely wanted.
    const float worldRadius = m_radius * std::max(scale.x, scale.y);
    return Circle{world.TransformPoint(m_offset), worldRadius};
}

int CircleColliderComponent::WorldShapes(ConvexShape* out, int maxOut) const {
    if (maxOut < 1) {
        return 0;
    }
    out[0] = ToConvex(WorldCircle());
    return 1;
}

float CircleColliderComponent::Area() const {
    // The LARGER scale, because that is the radius WorldCircle hands the
    // collision system - see the note there. The two must agree or the shape
    // the solver pushes is not the shape it weighs.
    const Vec2  scale  = OwnerWorldScale();
    const float radius = m_radius * std::max(scale.x, scale.y);
    return kPi * radius * radius;
}

// ---------------------------------------------------------------------------
//  CapsuleColliderComponent
// ---------------------------------------------------------------------------

void CapsuleColliderComponent::SetSize(Vec2 size) {
    m_size = Vec2{std::max(size.x, 0.0f), std::max(size.y, 0.0f)};
}

bool CapsuleColliderComponent::Deserialize(const Json& node, std::string& outError) {
    if (!ColliderComponent::Deserialize(node, outError)) {
        return false;
    }
    SetSize(ReadVec2(node, "size", m_size, kTypeName));

    const std::string direction = ReadString(node, "direction", "Vertical", kTypeName);
    if (direction == "Vertical") {
        m_direction = CapsuleDirection::Vertical;
    } else if (direction == "Horizontal") {
        m_direction = CapsuleDirection::Horizontal;
    } else {
        outError = "a capsule collider's \"direction\" should be \"Vertical\" or "
                   "\"Horizontal\"";
        return false;
    }
    return true;
}

bool CapsuleColliderComponent::Serialize(Json& out) const {
    if (!ColliderComponent::Serialize(out)) {
        return false;
    }
    WriteVec2(out, "size", m_size);
    out["direction"] =
        (m_direction == CapsuleDirection::Vertical) ? "Vertical" : "Horizontal";
    return true;
}

int CapsuleColliderComponent::WorldShapes(ConvexShape* out, int maxOut) const {
    if (maxOut < 1) {
        return 0;
    }

    // ======================================================================
    //  THE SIZE IS SCALED FIRST, AND THEN THE CAPSULE IS DERIVED FROM IT.
    //
    //  That order is Unity's and it is the whole fix. The radius used to be
    //  worked out from the UNSCALED size and then multiplied by
    //  max(scale.x, scale.y) - so a character stretched vertically, scale
    //  (1, 3), got a capsule three times too WIDE: twenty pixels of solid
    //  collider hanging off each side of the sprite, snagging on every wall
    //  and doorway it walked past. At scale (1, 10) it was ten times too wide.
    //
    //  Collider.h states the max() rule for a CIRCLE, where a non-uniform
    //  scale genuinely has no answer without an ellipse test - and then
    //  recommends a capsule for when that matters. A capsule copying the
    //  circle's compromise is the one thing it must not do: a capsule HAS a
    //  short axis and a long one, so each can take the scale of the axis it
    //  is actually on.
    // ======================================================================
    const Mat3 world = OwnerWorldMatrix();
    const Vec2 scale = OwnerWorldScale();

    const Vec2 scaled{m_size.x * scale.x, m_size.y * scale.y};

    // Unity's description turned into the engine's: the radius is half the
    // SHORT side, and whatever length is left after both round ends is the
    // core segment. A capsule as wide as it is tall has no core left and is a
    // circle, which ToConvex collapses for us.
    const bool  vertical = m_direction == CapsuleDirection::Vertical;
    const float breadth  = vertical ? scaled.x : scaled.y;
    const float length   = vertical ? scaled.y : scaled.x;

    const float radius  = breadth * 0.5f;
    const float coreLen = std::max(length - breadth, 0.0f) * 0.5f;

    // ======================================================================
    //  THE DIRECTION COMES FROM THE MATRIX, THE LENGTH FROM THE SCALED SIZE.
    //
    //  Those are two different questions and they used to be answered by one
    //  piece of arithmetic, badly in turn.
    //
    //  Putting the core offset through the FULL matrix scales it a second
    //  time, which is the bug that made a capsule under a vertical stretch
    //  come out the wrong length. Rotating it by the matrix's EXTRACTED
    //  rotation instead was worse in a way that is easy to miss: a rotated
    //  child under a non-uniformly scaled parent composes to a matrix that is
    //  NOT scale-then-rotate-then-translate - it SHEARS - and GetScale() and
    //  GetRotation() cannot describe such a matrix between them. Measured on
    //  a 45-degree child under a parent scaled (1,3): the extracted rotation
    //  was 71.6 degrees against a true core axis of 108.4, so both cap
    //  centres landed 32 px out. The full-matrix version had that case right.
    //
    //  So: the matrix transforms the local AXIS, which is correct under
    //  rotation, scale and shear alike, and the result is normalized to a
    //  pure direction. The distance along it is the core half-length derived
    //  from the scaled size, which is Unity's rule. The two cannot interfere.
    // ======================================================================
    // A degenerate matrix - an entity scaled to nothing on this axis - leaves
    // no direction to take, so the unrotated axis stands in. Overlap.h keeps
    // its own kTinyLength for this; it is file-local, so the number is here.
    constexpr float kNoDirection = 1e-12f;

    const Vec2 axis    = vertical ? Vec2{0.0f, 1.0f} : Vec2{1.0f, 0.0f};
    const Vec2 mapped  = world.TransformVector(axis);
    const Vec2 heading =
        mapped.LengthSquared() > kNoDirection ? mapped.Normalized() : axis;

    const Vec2 turned = heading * coreLen;
    const Vec2 centre = world.TransformPoint(m_offset);

    out[0] = ToConvex(Capsule{centre - turned, centre + turned, radius});
    return 1;
}

float CapsuleColliderComponent::Area() const {
    // The same arithmetic as WorldShapes, in the same order: scale the size,
    // then derive. Writing it the other way round is what made the collider
    // three times too wide under a vertical stretch.
    const Vec2  scale    = OwnerWorldScale();
    const Vec2  scaled{m_size.x * scale.x, m_size.y * scale.y};
    const bool  vertical = m_direction == CapsuleDirection::Vertical;
    const float breadth  = vertical ? scaled.x : scaled.y;
    const float length   = vertical ? scaled.y : scaled.x;
    const float radius   = breadth * 0.5f;
    const float coreLen  = std::max(length - breadth, 0.0f);

    // The middle rectangle plus the two half-discs that make up the ends.
    return breadth * coreLen + kPi * radius * radius;
}

// ---------------------------------------------------------------------------
//  PolygonColliderComponent
// ---------------------------------------------------------------------------

std::vector<Vec2> PolygonColliderComponent::DefaultSquare() {
    // 32 pixels across, anticlockwise. See the note on BoxCollider's default
    // for why this is not Unity's half-a-unit square.
    return {Vec2{-16.0f, -16.0f}, Vec2{16.0f, -16.0f},
            Vec2{16.0f, 16.0f},   Vec2{-16.0f, 16.0f}};
}

void PolygonColliderComponent::SetPoints(std::vector<Vec2> points) {
    if (points.size() > static_cast<std::size_t>(ConvexShape::kMaxPoints)) {
        ENGINE_LOG_ERROR(Channels::kPhysics,
                         "a polygon collider was given {} points and this engine "
                         "supports {}; the extra ones were dropped. Use several "
                         "colliders on one entity for a more detailed shape - see "
                         "physics/Collider.h",
                         points.size(), ConvexShape::kMaxPoints);
        points.resize(static_cast<std::size_t>(ConvexShape::kMaxPoints));
    }
    m_points = std::move(points);
}

bool PolygonColliderComponent::IsConvex() const {
    if (m_points.size() < 3) {
        return false;
    }

    // Walking the outline, every turn has to go the same way. A sign change
    // means a dent, and a dent is what the collision maths cannot represent.
    int sign = 0;
    for (std::size_t i = 0; i < m_points.size(); ++i) {
        const Vec2 a = m_points[i];
        const Vec2 b = m_points[(i + 1) % m_points.size()];
        const Vec2 c = m_points[(i + 2) % m_points.size()];

        const float turn = Cross(b - a, c - b);
        if (std::fabs(turn) < 1e-6f) {
            continue;   // three points in a line: no turn either way
        }
        const int thisSign = (turn > 0.0f) ? 1 : -1;
        if (sign == 0) {
            sign = thisSign;
        } else if (sign != thisSign) {
            return false;
        }
    }
    return true;
}

bool PolygonColliderComponent::Deserialize(const Json& node, std::string& outError) {
    if (!ColliderComponent::Deserialize(node, outError)) {
        return false;
    }

    std::vector<Vec2> points = m_points;
    if (!ReadPointList(node, "points", points, outError)) {
        return false;
    }
    if (points.size() < 3) {
        outError = "a polygon collider needs at least 3 points";
        return false;
    }
    SetPoints(std::move(points));

    if (!IsConvex()) {
        // Reported rather than refused. A dented outline still collides - as
        // its convex hull - so refusing to load the scene would be a harsher
        // response than the mistake deserves. Saying so is not optional
        // though: the shape on screen and the shape being tested differ.
        ENGINE_LOG_ERROR(Channels::kPhysics,
                         "a polygon collider's outline is not convex, so it will "
                         "collide as the convex hull of those points rather than as "
                         "drawn. Split it into several colliders instead.");
    }
    return true;
}

bool PolygonColliderComponent::Serialize(Json& out) const {
    if (!ColliderComponent::Serialize(out)) {
        return false;
    }
    WritePointList(out, "points", m_points);
    return true;
}

int PolygonColliderComponent::WorldShapes(ConvexShape* out, int maxOut) const {
    if (maxOut < 1 || m_points.size() < 3) {
        return 0;
    }

    const Mat3 world = OwnerWorldMatrix();
    Vec2       transformed[ConvexShape::kMaxPoints];
    const int  count = static_cast<int>(
        std::min(m_points.size(), static_cast<std::size_t>(ConvexShape::kMaxPoints)));

    for (int i = 0; i < count; ++i) {
        transformed[i] = world.TransformPoint(m_offset + m_points[static_cast<std::size_t>(i)]);
    }
    MakeAnticlockwise(transformed, count);

    out[0] = ToConvex(transformed, count, 0.0f);
    return 1;
}

// The box around the outline, in the entity's own space. A polygon is not a
// rectangle, so this overestimates how hard it is to spin - which errs toward
// calm, the same direction the old world-bounds version erred in and the
// right one for a number whose job is to make a shape tumble convincingly.
Vec2 PolygonColliderComponent::LocalSize() const {
    if (m_points.empty()) {
        return Vec2{0.0f, 0.0f};
    }
    Vec2 lo = m_points.front();
    Vec2 hi = lo;
    for (const Vec2& p : m_points) {
        lo.x = std::min(lo.x, p.x);
        lo.y = std::min(lo.y, p.y);
        hi.x = std::max(hi.x, p.x);
        hi.y = std::max(hi.y, p.y);
    }
    return hi - lo;
}

float PolygonColliderComponent::Area() const {
    const Vec2 scale = OwnerWorldScale();
    return PolygonArea(m_points) * scale.x * scale.y;
}

// ---------------------------------------------------------------------------
//  EdgeColliderComponent
// ---------------------------------------------------------------------------

std::vector<Vec2> EdgeColliderComponent::DefaultLine() {
    // 64 pixels long, so a new edge collider is something you can see and
    // grab. See the note on BoxCollider's default.
    return {Vec2{-32.0f, 0.0f}, Vec2{32.0f, 0.0f}};
}

void EdgeColliderComponent::SetPoints(std::vector<Vec2> points) {
    const auto maxPoints = static_cast<std::size_t>(kMaxEdgePoints);
    if (points.size() > maxPoints) {
        ENGINE_LOG_ERROR(Channels::kPhysics,
                         "an edge collider was given {} points and this engine "
                         "supports {}; the extra ones were dropped. Use several edge "
                         "colliders for a longer chain.",
                         points.size(), maxPoints);
        points.resize(maxPoints);
    }
    m_points = std::move(points);
}

void EdgeColliderComponent::SetEdgeRadius(float radius) {
    m_edgeRadius = std::max(radius, 0.0f);
}

bool EdgeColliderComponent::Deserialize(const Json& node, std::string& outError) {
    if (!ColliderComponent::Deserialize(node, outError)) {
        return false;
    }

    std::vector<Vec2> points = m_points;
    if (!ReadPointList(node, "points", points, outError)) {
        return false;
    }
    if (points.size() < 2) {
        outError = "an edge collider needs at least 2 points";
        return false;
    }
    SetPoints(std::move(points));

    SetEdgeRadius(ReadFloat(node, "edgeRadius", m_edgeRadius, kTypeName));
    m_loop = ReadBool(node, "loop", m_loop, kTypeName);
    return true;
}

bool EdgeColliderComponent::Serialize(Json& out) const {
    if (!ColliderComponent::Serialize(out)) {
        return false;
    }
    WritePointList(out, "points", m_points);
    if (m_edgeRadius != 0.0f) {
        out["edgeRadius"] = m_edgeRadius;
    }
    if (m_loop) {
        out["loop"] = m_loop;
    }
    return true;
}

int EdgeColliderComponent::WorldShapes(ConvexShape* out, int maxOut) const {
    if (m_points.size() < 2) {
        return 0;
    }

    const Mat3  world  = OwnerWorldMatrix();
    const Vec2  scale  = world.GetScale();
    const float radius = m_edgeRadius * std::max(scale.x, scale.y);

    // ONE SHAPE PER SEGMENT. A polyline is not convex, so it cannot be a
    // single ConvexShape - but it is trivially a list of two-point ones, and
    // the collision system already handles a collider that produces several.
    const int segments = static_cast<int>(m_points.size()) - (m_loop ? 0 : 1);

    int written = 0;
    for (int i = 0; i < segments && written < maxOut; ++i) {
        const Vec2 a = world.TransformPoint(
            m_offset + m_points[static_cast<std::size_t>(i)]);
        const Vec2 b = world.TransformPoint(
            m_offset + m_points[static_cast<std::size_t>((i + 1) % m_points.size())]);
        out[written++] = ToConvex(Capsule{a, b, radius});
    }
    return written;
}

int EdgeColliderComponent::WorldShapeCount() const {
    if (m_points.size() < 2) {
        return 0;
    }
    // Must agree with the `segments` calculation in WorldShapes above: a loop
    // has one segment per point, an open chain one fewer.
    return static_cast<int>(m_points.size()) - (m_loop ? 0 : 1);
}

// The box around the chain, inflated by its thickness. An edge collider has
// no area and gets no auto-mass - see Area below - but a body given one a
// mass by hand still has to be able to turn.
Vec2 EdgeColliderComponent::LocalSize() const {
    if (m_points.empty()) {
        return Vec2{0.0f, 0.0f};
    }
    Vec2 lo = m_points.front();
    Vec2 hi = lo;
    for (const Vec2& p : m_points) {
        lo.x = std::min(lo.x, p.x);
        lo.y = std::min(lo.y, p.y);
        hi.x = std::max(hi.x, p.x);
        hi.y = std::max(hi.y, p.y);
    }
    return (hi - lo) + Vec2{m_edgeRadius, m_edgeRadius} * 2.0f;
}

float EdgeColliderComponent::Area() const {
    // A line has no area. A body whose only collider is an edge therefore gets
    // no auto-mass, and Rigidbody2D falls back to its stated mass - which is
    // the right outcome, because an edge collider is scenery.
    return 0.0f;
}

// ---------------------------------------------------------------------------
//  Finding a collider of any shape
// ---------------------------------------------------------------------------

ColliderComponent* FindCollider(Entity& entity) {
    // dynamic_cast rather than comparing five type names, because the whole
    // point is not to care which of them it is - and a sixth shape should not
    // need this function edited.
    for (std::size_t i = 0; i < entity.ComponentCount(); ++i) {
        if (auto* collider = dynamic_cast<ColliderComponent*>(entity.ComponentAt(i));
            collider != nullptr) {
            return collider;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
//  CollisionSystem
// ---------------------------------------------------------------------------

void CollisionSystem::Register(ColliderComponent& collider) {
    // The id is handed out here and never reused, so a pair key is fixed for
    // as long as both colliders stay attached. See the note on ColliderId.
    g_colliders.push_back(
        ColliderRecord{&collider, collider.OwnerId(), g_nextColliderId++});
}

void CollisionSystem::Unregister(ColliderComponent& collider) {
    const auto it = std::find_if(g_colliders.begin(), g_colliders.end(),
                                 [&collider](const ColliderRecord& record) {
                                     return record.collider == &collider;
                                 });
    if (it == g_colliders.end()) {
        return;
    }

    const EntityId   owner = it->owner;
    const ColliderId id    = it->id;

    // EXIT WHEN ONE SIDE IS DESTROYED. Every pair this collider was part of
    // sends an exit to the OTHER entity, now, before the pair disappears.
    // Without this a door opened on enter never closes when the key that
    // opened it is destroyed inside the doorway.
    //
    // SENT AS THE SAME KIND OF EVENT THE ENTER WAS - trigger or collision -
    // which is what g_lastFrameTriggers is remembered for. A pickup that
    // destroys itself would otherwise send a trigger-enter and a
    // collision-exit, and a script listening for only one would never see the
    // pair close.
    // MATCHED ON THIS COLLIDER'S ID, NOT ON ITS OWNER'S, which used to be the
    // test. An entity with several colliders would otherwise lose the pairs
    // held through its SIBLINGS as well - and be told those overlaps had ended
    // while they were still perfectly live.
    for (auto pairIt = g_lastFrame.begin(); pairIt != g_lastFrame.end();) {
        if (pairIt->first.first == id || pairIt->first.second == id) {
            // The other entity, read out of the RECORD rather than worked out
            // from the key: an id cannot be turned back into an owner once its
            // collider is gone, and this one is going.
            const EntityId survivor = (pairIt->first.first == id)
                                          ? pairIt->second.b
                                          : pairIt->second.a;
            const char* type = pairIt->second.isTrigger
                                   ? MessageTypes::kTriggerExit
                                   : MessageTypes::kCollisionExit;
            // BOTH SIDES, exactly as the diff loop in Update does.
            //
            // The entity LOSING the collider used to be left out, so a script
            // on it saw an enter with no matching exit whenever one of its
            // colliders was removed - and with contacts keyed per collider
            // pair that is now a routine thing to do rather than an unusual
            // one. Unity closes the pairing on both sides.
            //
            // When the collider is going because its ENTITY is being
            // destroyed, that second message costs nothing: the dying entity
            // fails MessageBus::Dispatch's target check and its copy is
            // dropped. So this fixes the plain-removal case without changing
            // the destroy case at all.
            SendPairEvent(type, survivor, owner);
            SendPairEvent(type, owner, survivor);

            g_contacts.erase(pairIt->first);

            // erase() returns an iterator to the next element, which is what
            // makes it safe to remove entries while walking the map.
            pairIt = g_lastFrame.erase(pairIt);
        } else {
            ++pairIt;
        }
    }

    // And drop any entity-level index entry that pointed at one of this
    // collider's pairs, or a lookup would hand back a contact belonging to a
    // collider that no longer exists. Both indexes, for the same reason.
    for (std::map<EPair, CPair>* index : {&g_entityContact, &g_entityTrigger}) {
        for (auto entityIt = index->begin(); entityIt != index->end();) {
            if (entityIt->second.first == id || entityIt->second.second == id) {
                entityIt = index->erase(entityIt);
            } else {
                ++entityIt;
            }
        }
    }

    // Swap and pop, as with sprites. Nothing remembers a position in this list
    // - a collider is found by its pointer - so no fix-up is needed here.
    *it = g_colliders.back();
    g_colliders.pop_back();
}

void CollisionSystem::Clear() {
    g_eventPairs.clear();
    g_colliders.clear();
    g_lastFrame.clear();
    g_thisFrame.clear();
    g_contacts.clear();
    g_entityContact.clear();
    g_entityTrigger.clear();
    g_shapePool.clear();
    g_slices.clear();

    // g_nextColliderId IS DELIBERATELY NOT RESET. Reusing an id would let a
    // brand new collider inherit a pair that a destroyed one was part of, and
    // the only cost of not resetting is that the numbers keep climbing - which
    // nothing reads or stores.
}

void CollisionSystem::SetSolverIterations(int iterations) {
    g_solverIterations = std::clamp(iterations, 1, 32);
}

int CollisionSystem::SolverIterations() { return g_solverIterations; }

const Collision2D* CollisionSystem::ContactForEvent(int pairToken) {
    if (pairToken <= 0 ||
        static_cast<std::size_t>(pairToken) > g_eventPairs.size()) {
        return nullptr;
    }
    const auto it =
        g_contacts.find(g_eventPairs[static_cast<std::size_t>(pairToken - 1)]);
    return (it == g_contacts.end()) ? nullptr : &it->second;
}

const Collision2D* CollisionSystem::FindContact(EntityId a, EntityId b) {
    // THE DEEPEST SOLID CONTACT BETWEEN THESE TWO ENTITIES, or null - never a
    // trigger's, which is what the separate index is for.
    return LookUpByEntities(g_entityContact, a, b);
}

const Collision2D* CollisionSystem::FindTriggerContact(EntityId a, EntityId b) {
    // The same for the other kind. A trigger pair has a full contact - the
    // narrow phase never looks at IsTrigger, only the solver does - so this
    // carries a real normal, point and separation as well as both colliders.
    return LookUpByEntities(g_entityTrigger, a, b);
}

const std::vector<ColliderComponent*>& CollisionSystem::All() {
    // Rebuilt on request rather than kept in step with g_colliders, because
    // the queries in Physics2D want a plain list and the collision system
    // wants the owner ids alongside.
    static std::vector<ColliderComponent*> flat;
    flat.clear();
    flat.reserve(g_colliders.size());
    for (const ColliderRecord& record : g_colliders) {
        if (record.collider != nullptr) {
            flat.push_back(record.collider);
        }
    }
    return flat;
}

namespace {

// ---------------------------------------------------------------------------
//  THE SOLVER IS IN TWO HALVES, AND THEY ARE RUN DIFFERENT NUMBERS OF TIMES.
//
//  SolveVelocity exchanges momentum, and is run SEVERAL times. A body resting
//  on two others has to satisfy both contacts, and one pass can only satisfy
//  whichever it looked at last. Running it again is safe because it is
//  self-limiting: once a pair is no longer closing, `closing` is not negative
//  any more and the pass does nothing.
//
//  CorrectPositionPass un-overlaps them, and is ALSO run several times - but
//  it accumulates into g_corrections rather than moving anything, and each
//  pass RE-READS every separation from what the earlier passes accumulated.
//
//  THAT RE-READING IS THE WHOLE POINT, and getting it wrong has now cost two
//  separate bugs. The first: both halves in one function and four passes, each
//  moving the bodies by 80% of the SAME cached overlap, four times over -
//  which fired a body clear of the thing it had just landed on. The symptom
//  was a capsule bouncing off a floor with its bounciness set to zero. That
//  was fixed by correcting ONCE, which then caused the second: one sweep over
//  stale separations cannot satisfy a chain of contacts, so a six-box tower
//  settled 6.9 units inside the floor and stayed there.
//
//  "Recomputing the overlap between passes" sounded expensive and is not. A
//  correction is a pure translation, so the live separation is the original
//  plus a dot product - no shape is rebuilt and the narrow phase runs once.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
//  HOW MOVABLE EACH SIDE OF A CONTACT IS.
//
//  Dynamic is 1/m. Static, or no rigidbody at all, is zero: it cannot move,
//  and every other piece of the solver gets that for free by multiplying.
//
//  ==========================================================================
//  A KINEMATIC BODY IS ZERO AGAINST ANYTHING THAT CAN MOVE, AND MOVABLE
//  AGAINST ANYTHING THAT CANNOT - and that asymmetry is the whole fix.
//
//  Zero against a dynamic body is what makes a lift carry a crate and a door
//  shove a barrel without the barrel shoving back. That is what "infinite
//  mass, in effect" means in Rigidbody.h and it is unchanged.
//
//  But against STATIC scenery both sides were zero, the sum was zero, and
//  both halves of the solver returned without doing anything - so a kinematic
//  body went straight through a wall. Box2D and Unity behave that way too,
//  and for a moving platform it is defensible. It is not defensible here,
//  because Rigidbody.h recommends a kinematic body FOR A CHARACTER
//  CONTROLLER, and a character controller that walks through walls is not one.
//
//  So when the other side cannot move at all, the kinematic body is the only
//  thing in the contact that CAN, and it is the thing that gives way. The
//  VALUE does not matter, only that it is positive: with the other side at
//  zero it takes the whole correction either way, so 1 is written rather than
//  an invented mass.
//
//  TWO KINEMATIC BODIES STILL PASS THROUGH EACH OTHER. Each would see the
//  other as immovable and both would give way, which is a tug of war between
//  two things a script is positioning - and scripts that disagree about where
//  something is belong in the script, not in the solver. Box2D leaves them
//  alone and so does this.
// ---------------------------------------------------------------------------
void SolverMasses(Rigidbody2DComponent* a, Rigidbody2DComponent* b, float& outA,
                  float& outB) {
    const auto kind = [](Rigidbody2DComponent* body) {
        return body != nullptr ? body->BodyType() : RigidbodyType::Static;
    };

    const RigidbodyType typeA = kind(a);
    const RigidbodyType typeB = kind(b);

    outA = (a != nullptr) ? a->InverseMass() : 0.0f;
    outB = (b != nullptr) ? b->InverseMass() : 0.0f;

    const bool immovableA = (typeA == RigidbodyType::Static);
    const bool immovableB = (typeB == RigidbodyType::Static);

    if (typeA == RigidbodyType::Kinematic && immovableB) {
        outA = 1.0f;
    }
    if (typeB == RigidbodyType::Kinematic && immovableA) {
        outB = 1.0f;
    }
}

// Applies the solver's impulse to one side, using the inverse mass the solver
// decided on rather than the body's own - the two differ for exactly the
// kinematic case above.
void ApplySolved(Rigidbody2DComponent* body, float invMass, Vec2 impulse,
                 Vec2 point) {
    if (body == nullptr || invMass <= 0.0f) {
        return;
    }
    if (body->BodyType() == RigidbodyType::Dynamic) {
        // Its own inverse mass IS invMass here, and this also spins it.
        body->ApplyImpulse(impulse, point);
        return;
    }
    body->AddVelocity(impulse * invMass);
}

void SolveVelocity(const Collision2D& contact, ColliderComponent* a,
                   ColliderComponent* b) {
    Rigidbody2DComponent* bodyA = a->AttachedRigidbody();
    Rigidbody2DComponent* bodyB = b->AttachedRigidbody();

    float invMassA = 0.0f;
    float invMassB = 0.0f;
    SolverMasses(bodyA, bodyB, invMassA, invMassB);

    // NOTHING TO PUSH. Two static colliders, or two kinematic ones: both have
    // infinite mass, so there is no answer to "which one moves". Unity says
    // the same thing by refusing to simulate such a pair.
    //
    // A kinematic body against a STATIC one is no longer in that set - see
    // SolverMasses, which is where the asymmetry lives.
    const float invMassSum = invMassA + invMassB;
    if (invMassSum <= 0.0f) {
        return;
    }

    // The contact normal in Collision2D points toward the RECEIVER, and this
    // copy was built for A - so it points from B into A, and A separates by
    // moving ALONG it.
    const Vec2 normal = contact.normal;
    const Vec2 point  = contact.point;

    const float invInertiaA = (bodyA != nullptr) ? bodyA->InverseInertia() : 0.0f;
    const float invInertiaB = (bodyB != nullptr) ? bodyB->InverseInertia() : 0.0f;

    const Transform2D* transformA = a->OwnerTransform();
    const Transform2D* transformB = b->OwnerTransform();
    const Vec2 centreA = (transformA != nullptr) ? transformA->WorldPosition() : point;
    const Vec2 centreB = (transformB != nullptr) ? transformB->WorldPosition() : point;

    const Vec2 armA = point - centreA;
    const Vec2 armB = point - centreB;

    const Vec2 velocityA = (bodyA != nullptr) ? bodyA->VelocityAtPoint(point) : Vec2{};
    const Vec2 velocityB = (bodyB != nullptr) ? bodyB->VelocityAtPoint(point) : Vec2{};

    // How fast A is closing on B along the normal. Positive means A is moving
    // along the normal, which points away from B - so they are separating
    // already and an impulse would be pulling them back together.
    const Vec2  relative  = velocityA - velocityB;
    const float closing   = Dot(relative, normal);

    if (closing >= 0.0f) {
        return;   // already separating; an impulse would pull them together
    }

    {
        // A CONTACT BARELY CLOSING DOES NOT BOUNCE. See g_restingSpeed: the
        // alternative is a bouncy body that never settles.
        const float restitution =
            (-closing > g_restingSpeed)
                ? std::max(a->Material().bounciness, b->Material().bounciness)
                : 0.0f;

        // The rotational part: a contact far from the centre is easier to spin
        // than to shove, and Cross(arm, normal) is how much of an impulse goes
        // into spin rather than into motion.
        const float spinA = Cross(armA, normal);
        const float spinB = Cross(armB, normal);
        const float effectiveMass = invMassSum + invInertiaA * spinA * spinA +
                                    invInertiaB * spinB * spinB;

        if (effectiveMass > 0.0f) {
            const float magnitude = -(1.0f + restitution) * closing / effectiveMass;
            const Vec2  impulse   = normal * magnitude;

            ApplySolved(bodyA, invMassA, impulse, point);
            ApplySolved(bodyB, invMassB, -impulse, point);

            // ---- friction, along the surface rather than through it --------
            //
            // Recomputed from the velocity AFTER the normal impulse, because
            // the amount of grip available depends on how hard the surfaces
            // were just pressed together.
            const Vec2 afterA =
                (bodyA != nullptr) ? bodyA->VelocityAtPoint(point) : Vec2{};
            const Vec2 afterB =
                (bodyB != nullptr) ? bodyB->VelocityAtPoint(point) : Vec2{};
            const Vec2 slide = afterA - afterB;

            Vec2 tangent = slide - normal * Dot(slide, normal);
            if (tangent.LengthSquared() > 1e-8f) {
                tangent = tangent.Normalized();

                const float spinTA = Cross(armA, tangent);
                const float spinTB = Cross(armB, tangent);
                const float tangentMass = invMassSum + invInertiaA * spinTA * spinTA +
                                          invInertiaB * spinTB * spinTB;

                if (tangentMass > 0.0f) {
                    // Box2D's combine rule, which is what Unity 2D uses
                    // underneath: the geometric mean, so one slippery surface
                    // is enough to make a contact slippery.
                    const float friction = std::sqrt(a->Material().friction *
                                                     b->Material().friction);

                    float tangentMagnitude = -Dot(slide, tangent) / tangentMass;

                    // COULOMB'S LIMIT: friction can only ever resist, never
                    // drive. Without this clamp a sliding box would be dragged
                    // backwards up a slope by its own friction.
                    const float limit = friction * std::fabs(magnitude);
                    tangentMagnitude  = std::clamp(tangentMagnitude, -limit, limit);

                    const Vec2 frictionImpulse = tangent * tangentMagnitude;
                    ApplySolved(bodyA, invMassA, frictionImpulse, point);
                    ApplySolved(bodyB, invMassB, -frictionImpulse, point);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  THE POSITION HALF OF THE SOLVER, which un-overlaps what the velocity half
//  has already stopped.
//
//  ONE CONTACT, ONE PASS. The caller sweeps the whole contact list several
//  times; see the loop in Update.
//
//  ==========================================================================
//  IT ACCUMULATES RATHER THAN MOVING ANYTHING, and the separation it works
//  from is RE-READ rather than taken as given. Both of those are the fix for
//  a single stale sweep, which was wrong in two opposite directions at once:
//
//    THREE CONTACTS PUSHING THE SAME WAY. A compound body resting flat on the
//    ground - three box colliders on one entity, which physics/Collider.h
//    recommends - had three contacts, all of depth d, and each one moved it
//    0.8d. Total 2.4d: it was fired clear of the thing it had just landed on,
//    fell back, and did it again.
//
//    TWO CONTACTS PUSHING OPPOSITE WAYS. An interior box in a stack is pushed
//    up out of the box below and down out of the box above by nearly the same
//    amount, so the two cancelled and the overlap never cleared at all. A
//    six-box tower settled with its bottom box 6.9 units inside the floor and
//    its neighbours 13 units inside each other, and stayed that way for
//    twenty seconds.
//
//  ==========================================================================
//  AND IT COSTS NO GEOMETRY TO RE-READ. A correction is a pure translation,
//  so a contact's live separation is its original separation plus how far the
//  two bodies have since been moved ALONG ITS NORMAL. That is one dot product
//  per contact per pass - no shape is rebuilt, no narrow phase is re-run, and
//  nothing has to be re-gathered. The normal points from B toward A (A
//  separates by moving along it; see SolveVelocity), so moving A along the
//  normal and B against it both widen the gap.
// ---------------------------------------------------------------------------
void CorrectPositionPass(const Collision2D& contact, ColliderComponent* a,
                         ColliderComponent* b) {
    Rigidbody2DComponent* bodyA = a->AttachedRigidbody();
    Rigidbody2DComponent* bodyB = b->AttachedRigidbody();

    float invMassA = 0.0f;
    float invMassB = 0.0f;
    SolverMasses(bodyA, bodyB, invMassA, invMassB);

    const float invMassSum = invMassA + invMassB;
    if (invMassSum <= 0.0f) {
        return;
    }

    // THE SLOP IS NOT A FUDGE. Correcting every last fraction of a unit makes
    // a resting body jitter: it is pushed out, gravity pulls it back in, and
    // it buzzes forever. Leaving a sliver of overlap alone lets it settle, and
    // the sliver is far too small to see.
    //
    // AND THE FRACTION IS NOT ONE. Taking 80% of whatever is LEFT each pass
    // converges on the right answer without ever overshooting it - which is
    // the whole reason this can now be iterated at all.
    constexpr float kSlop     = 0.01f;
    constexpr float kFraction = 0.8f;

    const auto moved = [](const std::unordered_map<Rigidbody2DComponent*, Vec2>& map,
                          Rigidbody2DComponent* body) {
        if (body == nullptr) {
            return Vec2{0.0f, 0.0f};
        }
        const auto it = map.find(body);
        return it != map.end() ? it->second : Vec2{0.0f, 0.0f};
    };

    const Vec2  deltaA = moved(g_corrections, bodyA);
    const Vec2  deltaB = moved(g_corrections, bodyB);
    const float live   = contact.separation + Dot(deltaA - deltaB, contact.normal);

    const float depth = std::max(-live - kSlop, 0.0f);
    if (depth <= 0.0f) {
        return;   // already clear, or clear enough
    }

    const Vec2 correction = contact.normal * (depth * kFraction / invMassSum);
    if (bodyA != nullptr && invMassA > 0.0f) {
        g_corrections[bodyA] += correction * invMassA;
    }
    if (bodyB != nullptr && invMassB > 0.0f) {
        g_corrections[bodyB] -= correction * invMassB;
    }
}

} // namespace

void CollisionSystem::Update(float fixedStep) {
    Scene* scene = Scene::Active();
    if (scene == nullptr) {
        return;
    }

    // Two steps' worth of falling, which is the speed a body at rest on the
    // floor picks up and gives back every step. See g_restingSpeed.
    g_restingSpeed = std::fabs(Physics2D::Gravity().y) * fixedStep * 2.0f;

    g_thisFrame.clear();
    g_contacts.clear();
    g_entityContact.clear();
    g_entityTrigger.clear();
    g_eventPairs.clear();
    g_narrowPhaseTests = 0;
    g_broadPhasePairs  = 0;

    // ---- gather the shapes, once per collider rather than once per pair ----
    //
    // A collider's world shapes cost a matrix multiply per point. Building
    // them inside the pair loop would redo that work for every one of its
    // neighbours.
    g_shapePool.clear();
    g_slices.clear();

    struct Live {
        ColliderComponent* collider = nullptr;
        EntityId           owner{};
        ColliderId         id    = 0;
        int                slice = 0;
    };
    std::vector<Live> live;
    live.reserve(g_colliders.size());

    // Reused across every collider in the loop below, so the whole gather
    // allocates once however many shapes the biggest collider turns out to
    // have - a tilemap collider alone can produce hundreds.
    std::vector<ConvexShape> scratch;

    for (const ColliderRecord& record : g_colliders) {
        ColliderComponent* collider = record.collider;
        if (collider == nullptr || !collider->Enabled()) {
            continue;
        }

        // A switched-off entity does not collide and is invisible to every
        // Physics2D query. The pair-set diff below turns that into proper
        // exit events for whatever it was touching, exactly as destroying it
        // would - see Entity::IsActiveInHierarchy.
        if (!collider->OwnerIsActive()) {
            continue;
        }

        // Something queued for destruction this tick still draws and still
        // updates, but does NOT collide - see rule 1 in DeferredOps.h.
        // "Destroyed but still hurting you" is a genuinely confusing bug.
        if (DeferredOps::IsPendingDestroy(record.owner) || !scene->IsValid(record.owner)) {
            continue;
        }

        const int count = GatherWorldShapes(*collider, scratch);
        if (count <= 0) {
            continue;
        }

        ShapeSlice slice;
        slice.start  = static_cast<int>(g_shapePool.size());
        slice.count  = count;
        slice.bounds = scratch[0].Bounds();
        for (int i = 0; i < count; ++i) {
            g_shapePool.push_back(scratch[i]);
            if (i > 0) {
                const AABB other = scratch[i].Bounds();
                slice.bounds.Encapsulate(other.min);
                slice.bounds.Encapsulate(other.max);
            }
        }

        live.push_back(
            Live{collider, record.owner, record.id, static_cast<int>(g_slices.size())});
        g_slices.push_back(slice);
    }

    // ---- BROAD PHASE: sort on one axis, then sweep ------------------------
    //
    // The old system tested every collider against every other one - fine for
    // a few dozen, quadratic for anything more. Sorting by the left edge means
    // the inner loop can STOP as soon as it reaches something that starts to
    // the right of where the outer one ends: everything after that is further
    // right still, so no later pair can possibly overlap.
    //
    // For scattered objects that turns the quadratic into roughly linear. The
    // worst case - everything piled on one spot - is still quadratic, and that
    // is unavoidable: if everything really does overlap, every pair really is
    // a contact.
    std::sort(live.begin(), live.end(), [](const Live& lhs, const Live& rhs) {
        return g_slices[static_cast<std::size_t>(lhs.slice)].bounds.min.x <
               g_slices[static_cast<std::size_t>(rhs.slice)].bounds.min.x;
    });

    g_bruteForcePairs = live.size() * (live.size() > 0 ? live.size() - 1 : 0) / 2;

    for (std::size_t i = 0; i < live.size(); ++i) {
        const ShapeSlice& sliceA = g_slices[static_cast<std::size_t>(live[i].slice)];

        for (std::size_t j = i + 1; j < live.size(); ++j) {
            const ShapeSlice& sliceB = g_slices[static_cast<std::size_t>(live[j].slice)];

            // The sweep's early exit. Sorted on min.x, so once this is true it
            // is true for every remaining j.
            ++g_broadPhasePairs;

            if (sliceB.bounds.min.x > sliceA.bounds.max.x) {
                break;
            }

            // ==============================================================
            //  TWO COLLIDERS ON ONE ENTITY DO NOT COLLIDE WITH EACH OTHER.
            //
            //  Unity's rule, and without it a compound body - which
            //  Collider.h recommends building as several colliders on the one
            //  entity that carries the rigidbody - OVERLAPS ITSELF. That
            //  stored a real contact, and the event loop below sends every
            //  pair's event to both of its sides, so the entity was told it
            //  had touched itself, twice, every step.
            //
            //  Guarded before the key is formed rather than after, so the
            //  degenerate pair cannot reach any container, cannot inflate the
            //  touching-pair count and cannot turn the gizmo red. It costs one
            //  comparison of two entity ids.
            //
            //  The cost is that siblings can no longer push each other apart.
            //  Nothing in the engine, the assets or the tests wants that, and
            //  a compound body pushing itself apart was never the intent.
            // ==============================================================
            if (live[i].owner == live[j].owner) {
                continue;
            }

            // Still worth the cheap y test before the expensive shape test.
            if (!Overlaps(sliceA.bounds, sliceB.bounds)) {
                continue;
            }
            if (!LayersInterested(*live[i].collider, *live[j].collider)) {
                continue;
            }

            // ---- NARROW PHASE: the real shapes --------------------------
            //
            // The DEEPEST contact between any pair of sub-shapes wins. That
            // matters for an edge collider, where several of its segments can
            // touch the same body at once and only the worst overlap should
            // decide which way it is pushed out.
            Contact deepest;
            for (int sa = 0; sa < sliceA.count; ++sa) {
                for (int sb = 0; sb < sliceB.count; ++sb) {
                    ++g_narrowPhaseTests;
                    const Contact contact =
                        Collide(g_shapePool[static_cast<std::size_t>(sliceA.start + sa)],
                                g_shapePool[static_cast<std::size_t>(sliceB.start + sb)]);
                    if (contact.touching &&
                        (!deepest.touching || contact.separation < deepest.separation)) {
                        deepest = contact;
                    }
                }
            }

            if (!deepest.touching) {
                continue;
            }

            const CPair pair = MakePair(live[i].id, live[j].id);

            // THE KIND, worked out here where both colliders are already in
            // hand. A trigger on EITHER side makes the pair a trigger pair;
            // the solver below asks the opposite question, because solid()
            // needs both of them to be non-triggers.
            //
            // Recorded rather than re-derived in the event loop, which used to
            // look the contact up again to ask it - one map find per touching
            // pair, every step, for a bool that was known right here.
            const bool isTrigger =
                live[i].collider->IsTrigger() || live[j].collider->IsTrigger();

            // Stored once, oriented for whichever COLLIDER comes first in the
            // pair. The dispatcher flips it for the other side.
            Collision2D info;
            info.other         = live[j].owner;
            info.otherCollider = live[j].collider;
            info.thisCollider  = live[i].collider;
            info.point         = deepest.point;
            info.separation    = deepest.separation;

            // Collide() gives a normal from A toward B; Collision2D wants one
            // pointing at the receiver, so it is reversed here once rather
            // than at every use.
            info.normal = -deepest.normal;

            Rigidbody2DComponent* bodyA = live[i].collider->AttachedRigidbody();
            Rigidbody2DComponent* bodyB = live[j].collider->AttachedRigidbody();
            const Vec2 velA = (bodyA != nullptr) ? bodyA->VelocityAtPoint(deepest.point)
                                                 : Vec2{};
            const Vec2 velB = (bodyB != nullptr) ? bodyB->VelocityAtPoint(deepest.point)
                                                 : Vec2{};
            info.relativeVelocity = velB - velA;

            // Keyed so that `first` of the pair is the COLLIDER `info`
            // describes, which is what lets the dispatcher know when to flip.
            const bool firstIsI = pair.first == live[i].id;
            if (!firstIsI) {
                info.other         = live[i].owner;
                info.otherCollider = live[i].collider;
                info.thisCollider  = live[j].collider;
                info.normal        = deepest.normal;
                info.relativeVelocity = velA - velB;
            }

            // THE OWNERS GO IN THE RECORD, because an id cannot be turned back
            // into an entity once its collider is gone - and that is exactly
            // the moment Unregister has to name the surviving side of the pair.
            g_thisFrame.insert_or_assign(
                pair, PairInfo{firstIsI ? live[i].owner : live[j].owner,
                               firstIsI ? live[j].owner : live[i].owner, isTrigger});

            // insert_or_assign AND NOT emplace. The loop above reaches each
            // collider pair exactly once, so this never actually replaces
            // anything - but "silently keep the first and drop the rest" is
            // the precise failure this key exists to remove, and leaving the
            // call that did it sitting in the file would invite it back.
            g_contacts.insert_or_assign(pair, info);

            // ---- the entity-level index, which is all a lookup can use -----
            //
            // INTO THE INDEX FOR THIS PAIR'S KIND. See the note on the two
            // maps: keeping them apart is what stops a collision hook being
            // handed a trigger's record, and a trigger hook a solid one.
            {
                std::map<EPair, CPair>& index =
                    isTrigger ? g_entityTrigger : g_entityContact;

                const EPair entities = MakeEntityPair(live[i].owner, live[j].owner);
                const auto  existing = index.find(entities);
                if (existing == index.end()) {
                    index.emplace(entities, pair);
                } else {
                    // Separation is NEGATIVE when they overlap, so deeper is
                    // smaller. A missing held contact cannot happen - it was
                    // stored in the same iteration - but answering with the
                    // newer one if it ever did is the safe direction.
                    const auto held = g_contacts.find(existing->second);
                    if (held == g_contacts.end() ||
                        deepest.separation < held->second.separation) {
                        existing->second = pair;
                    }
                }
            }
        }
    }

    // ---- RESOLVE, before any event goes out -------------------------------
    //
    // Unity's order too: the physics step finishes its work and THEN tells
    // anybody about it. A script that reads a transform in OnCollisionEnter
    // therefore sees where things ended up, not a position halfway through
    // being corrected.
    // A trigger is not solid: it reports and nothing else. Worked out once
    // rather than inside both loops below.
    const auto solid = [](const Collision2D& contact) {
        return contact.thisCollider != nullptr && contact.otherCollider != nullptr &&
               !contact.thisCollider->IsTrigger() && !contact.otherCollider->IsTrigger();
    };

    for (int pass = 0; pass < g_solverIterations; ++pass) {
        for (auto& [pair, contact] : g_contacts) {
            if (solid(contact)) {
                SolveVelocity(contact, contact.thisCollider, contact.otherCollider);
            }
        }
    }

    // ======================================================================
    //  AND THEN THE POSITIONS, swept several times and applied once.
    //
    //  SEVERAL TIMES, because one sweep cannot satisfy a chain: clearing the
    //  overlap between the floor and the bottom box of a tower moves that box
    //  up into the one above it, and only another pass notices. Four passes
    //  takes a six-box tower from 6.9 units of sink to a fraction of one; the
    //  number is its own constant rather than borrowing solverIterations,
    //  because the two halves converge at different rates and tying them
    //  together would make tuning one retune the other.
    //
    //  APPLIED ONCE, at the end, because a pass has to read the separations
    //  the previous pass produced - and it reads them from the accumulated
    //  translations rather than from the shapes, so nothing may move until
    //  every pass is done. See CorrectPositionPass.
    // ======================================================================
    constexpr int kPositionPasses = 4;

    g_corrections.clear();
    for (int pass = 0; pass < kPositionPasses; ++pass) {
        for (auto& [pair, contact] : g_contacts) {
            if (solid(contact)) {
                CorrectPositionPass(contact, contact.thisCollider,
                                    contact.otherCollider);
            }
        }
    }
    for (const auto& [body, delta] : g_corrections) {
        body->Translate(delta);
    }

    // ---- the events -------------------------------------------------------
    //
    // Compare this step's pairs against last step's. BOTH entities get every
    // event, each naming the other as the partner.
    for (const auto& [pair, info] : g_thisFrame) {
        const auto lastIt      = g_lastFrame.find(pair);
        bool       wasTouching = lastIt != g_lastFrame.end();

        // ==================================================================
        //  THE KIND CHANGED WHILE THEY WERE STILL TOUCHING, which a script
        //  can cause at any moment: SetTrigger is public and nothing stops it
        //  being called from inside a handler.
        //
        //  Without this the pair would simply start sending the OTHER
        //  family's stay - an OnTriggerStay for something that never sent an
        //  OnTriggerEnter, and an OnCollisionEnter that never gets its exit.
        //  So the old lifecycle is closed first and the new one opens as a
        //  fresh enter, which is the only reading under which every enter has
        //  a matching exit.
        // ==================================================================
        if (wasTouching && lastIt->second.isTrigger != info.isTrigger) {
            const char* closing = lastIt->second.isTrigger
                                      ? MessageTypes::kTriggerExit
                                      : MessageTypes::kCollisionExit;
            SendPairEvent(closing, info.a, info.b);
            SendPairEvent(closing, info.b, info.a);
            wasTouching = false;
        }

        const char* type =
            info.isTrigger ? (wasTouching ? MessageTypes::kTriggerStay
                                          : MessageTypes::kTriggerEnter)
                           : (wasTouching ? MessageTypes::kCollisionStay
                                          : MessageTypes::kCollisionEnter);

        const int token = TokenFor(pair);
        SendPairEvent(type, info.a, info.b, token);
        SendPairEvent(type, info.b, info.a, token);
    }

    for (const auto& [pair, info] : g_lastFrame) {
        if (g_thisFrame.contains(pair)) {
            continue;
        }
        // Exit: it was touching last step and is not now. Sent as the same
        // KIND of event the enter was, which is what the flag on the record is
        // remembered for - and remembering it rather than asking the colliders
        // is what makes this work when one of them has just been destroyed.
        const char* type = info.isTrigger ? MessageTypes::kTriggerExit
                                          : MessageTypes::kCollisionExit;
        SendPairEvent(type, info.a, info.b);
        SendPairEvent(type, info.b, info.a);
    }

    g_lastFrame = g_thisFrame;
}

// ---------------------------------------------------------------------------
//  The collider outlines. See the long note on the declaration for why this
//  is not the tail of Update any more.
// ---------------------------------------------------------------------------
void CollisionSystem::DrawGizmos(bool liveContacts) {
    if (!Gizmos::IsCategoryEnabled(GizmoCategory::Colliders)) {
        return;
    }

    // Kept between calls rather than built on each one, exactly as
    // WorldBounds does it: this runs once a frame over every collider in the
    // scene, and a tilemap collider alone produces hundreds of shapes. After
    // the first frame it asks the system for nothing.
    static std::vector<ConvexShape> shapes;

    for (const ColliderRecord& record : g_colliders) {
        ColliderComponent* collider = record.collider;
        if (collider == nullptr) {
            continue;
        }

        Color color = Color::Green();
        if (!collider->Enabled()) {
            color = Color{110, 110, 110, 255};
        } else if (collider->IsTrigger()) {
            color = Color::Cyan();
        } else if (liveContacts) {
            // THIS COLLIDER, not its entity. A collider used to turn red
            // because a SIBLING was touching something, which is not what
            // the colour claims to mean.
            const bool touching =
                std::any_of(g_thisFrame.begin(), g_thisFrame.end(),
                            [&](const auto& entry) {
                                return entry.first.first == record.id ||
                                       entry.first.second == record.id;
                            });
            if (touching) {
                color = Color::Red();
            }
        }

        const int count = GatherWorldShapes(*collider, shapes);
        for (int i = 0; i < count; ++i) {
            DrawShapeGizmo(shapes[i], color);
        }
    }
}

std::size_t CollisionSystem::ColliderCount()    { return g_colliders.size(); }
std::size_t CollisionSystem::ActivePairCount()  { return g_lastFrame.size(); }
std::size_t CollisionSystem::NarrowPhaseTests() { return g_narrowPhaseTests; }
std::size_t CollisionSystem::BroadPhasePairs()  { return g_broadPhasePairs; }
std::size_t CollisionSystem::BruteForcePairs()  { return g_bruteForcePairs; }

void CollisionSystem::RegisterComponentTypes() {
    const auto box = []() -> std::unique_ptr<Component> {
        return std::make_unique<BoxColliderComponent>();
    };

    ComponentFactory::Register(BoxColliderComponent::kTypeName, box);

    // THE OLD NAME, registered to the same class so that every scene file
    // written before boxes could rotate still loads. It is not deprecated so
    // much as superseded: the shape is the same, it is just no longer always
    // axis-aligned, so the "AABB" in the name had stopped being true.
    ComponentFactory::RegisterAlias(BoxColliderComponent::kLegacyTypeName, box);

    ComponentFactory::Register(CircleColliderComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<CircleColliderComponent>();
                               });
    ComponentFactory::Register(CapsuleColliderComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<CapsuleColliderComponent>();
                               });
    ComponentFactory::Register(PolygonColliderComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<PolygonColliderComponent>();
                               });
    ComponentFactory::Register(EdgeColliderComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<EdgeColliderComponent>();
                               });
}

} // namespace eng
