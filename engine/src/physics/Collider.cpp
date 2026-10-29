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
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace eng {
namespace {

// ---------------------------------------------------------------------------
//  The collider list, and the shapes for this step
// ---------------------------------------------------------------------------

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

Pair MakePair(EntityId a, EntityId b) {
    return (a < b) ? Pair{a, b} : Pair{b, a};
}

std::set<Pair> g_lastFrame;
std::set<Pair> g_thisFrame;

// Which pairs were TRIGGER pairs last step, so that an exit can be sent as the
// same kind of event as the enter was. Without this, a pickup destroyed while
// overlapping would send a trigger-enter and a collision-exit, and a script
// listening for only one of them would never see the pair close.
std::set<Pair> g_lastFrameTriggers;

// The contact for every touching pair this step, so that the script
// dispatcher can attach it to the event. Keyed the same way as the pair sets.
std::map<Pair, Collision2D> g_contacts;

bool g_collisionsSubscribed = false;

int         g_solverIterations = 4;
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

void SendPairEvent(const char* type, EntityId to, EntityId partner) {
    Message message;
    message.type   = type;
    message.target = to;
    message.sender = partner;
    message.other  = partner;
    MessageBus::Send(message);
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
    return 4.0f * m_halfExtents.x * m_halfExtents.y;
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
    return kPi * m_radius * m_radius;
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

    // Unity's description turned into the engine's: the radius is half the
    // SHORT side, and whatever length is left after both round ends is the
    // core segment. A capsule as wide as it is tall has no core left and is a
    // circle, which ToConvex collapses for us.
    const bool  vertical = m_direction == CapsuleDirection::Vertical;
    const float breadth  = vertical ? m_size.x : m_size.y;
    const float length   = vertical ? m_size.y : m_size.x;

    const float radius  = breadth * 0.5f;
    const float coreLen = std::max(length - breadth, 0.0f) * 0.5f;

    const Vec2 along = vertical ? Vec2{0.0f, coreLen} : Vec2{coreLen, 0.0f};

    const Mat3 world = OwnerWorldMatrix();
    const Vec2 scale = world.GetScale();

    // The ends go through the full matrix, so rotation and the parent chain
    // come for free. The radius takes the larger scale, for the same reason a
    // circle's does.
    const Capsule capsule{world.TransformPoint(m_offset - along),
                          world.TransformPoint(m_offset + along),
                          radius * std::max(scale.x, scale.y)};

    out[0] = ToConvex(capsule);
    return 1;
}

float CapsuleColliderComponent::Area() const {
    const bool  vertical = m_direction == CapsuleDirection::Vertical;
    const float breadth  = vertical ? m_size.x : m_size.y;
    const float length   = vertical ? m_size.y : m_size.x;
    const float radius   = breadth * 0.5f;
    const float coreLen  = std::max(length - breadth, 0.0f);

    // The middle rectangle plus the two half-discs that make up the ends.
    return breadth * coreLen + kPi * radius * radius;
}

// ---------------------------------------------------------------------------
//  PolygonColliderComponent
// ---------------------------------------------------------------------------

std::vector<Vec2> PolygonColliderComponent::DefaultSquare() {
    return {Vec2{-0.5f, -0.5f}, Vec2{0.5f, -0.5f}, Vec2{0.5f, 0.5f}, Vec2{-0.5f, 0.5f}};
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

float PolygonColliderComponent::Area() const {
    return PolygonArea(m_points);
}

// ---------------------------------------------------------------------------
//  EdgeColliderComponent
// ---------------------------------------------------------------------------

std::vector<Vec2> EdgeColliderComponent::DefaultLine() {
    return {Vec2{-1.0f, 0.0f}, Vec2{1.0f, 0.0f}};
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
    //
    // SENT AS THE SAME KIND OF EVENT THE ENTER WAS - trigger or collision -
    // which is what g_lastFrameTriggers is remembered for. A pickup that
    // destroys itself would otherwise send a trigger-enter and a
    // collision-exit, and a script listening for only one would never see the
    // pair close.
    for (auto pairIt = g_lastFrame.begin(); pairIt != g_lastFrame.end();) {
        if (pairIt->first == owner || pairIt->second == owner) {
            const EntityId survivor = (pairIt->first == owner) ? pairIt->second
                                                               : pairIt->first;
            const char* type = g_lastFrameTriggers.contains(*pairIt)
                                   ? MessageTypes::kTriggerExit
                                   : MessageTypes::kCollisionExit;
            SendPairEvent(type, survivor, owner);

            g_lastFrameTriggers.erase(*pairIt);
            g_contacts.erase(*pairIt);

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
    g_lastFrameTriggers.clear();
    g_contacts.clear();
    g_shapePool.clear();
    g_slices.clear();
}

void CollisionSystem::SetSolverIterations(int iterations) {
    g_solverIterations = std::clamp(iterations, 1, 32);
}

int CollisionSystem::SolverIterations() { return g_solverIterations; }

const Collision2D* CollisionSystem::FindContact(EntityId a, EntityId b) {
    const auto it = g_contacts.find(MakePair(a, b));
    return (it == g_contacts.end()) ? nullptr : &it->second;
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
//  CorrectPosition un-overlaps them, and is run ONCE. It is NOT self-limiting,
//  and that difference cost a real bug worth recording: with both halves in
//  one function and four solver passes, each pass moved the bodies by 80% of
//  the SAME cached overlap - four times over - which fired a body clear of the
//  thing it had just landed on. The symptom was a capsule bouncing off the
//  floor with its bounciness set to zero, and an OnCollisionEnter every few
//  frames as the pair separated and met again.
//
//  Recomputing the overlap between passes would also have fixed it, at the
//  price of redoing the narrow phase several times a step. Correcting once is
//  cheaper and reads more honestly: the velocity constraint is iterative, the
//  position fix is a one-off nudge.
// ---------------------------------------------------------------------------
void SolveVelocity(const Collision2D& contact, ColliderComponent* a,
                   ColliderComponent* b) {
    Rigidbody2DComponent* bodyA = a->AttachedRigidbody();
    Rigidbody2DComponent* bodyB = b->AttachedRigidbody();

    const float invMassA = (bodyA != nullptr) ? bodyA->InverseMass() : 0.0f;
    const float invMassB = (bodyB != nullptr) ? bodyB->InverseMass() : 0.0f;

    // NOTHING TO PUSH. Two static colliders, or two kinematic ones: both have
    // infinite mass, so there is no answer to "which one moves". Unity says
    // the same thing by refusing to simulate such a pair.
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
        const float restitution =
            std::max(a->Material().bounciness, b->Material().bounciness);

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

            if (bodyA != nullptr) { bodyA->ApplyImpulse(impulse, point); }
            if (bodyB != nullptr) { bodyB->ApplyImpulse(-impulse, point); }

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
                    if (bodyA != nullptr) { bodyA->ApplyImpulse(frictionImpulse, point); }
                    if (bodyB != nullptr) { bodyB->ApplyImpulse(-frictionImpulse, point); }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  Un-overlapping them. Run ONCE per step - see the note above.
// ---------------------------------------------------------------------------
void CorrectPosition(const Collision2D& contact, ColliderComponent* a,
                     ColliderComponent* b) {
    Rigidbody2DComponent* bodyA = a->AttachedRigidbody();
    Rigidbody2DComponent* bodyB = b->AttachedRigidbody();

    const float invMassA = (bodyA != nullptr) ? bodyA->InverseMass() : 0.0f;
    const float invMassB = (bodyB != nullptr) ? bodyB->InverseMass() : 0.0f;
    const float invMassSum = invMassA + invMassB;
    if (invMassSum <= 0.0f) {
        return;
    }

    // An impulse changes velocity, which fixes the NEXT step. The overlap that
    // already exists has to be undone by moving them, split in proportion to
    // how movable each one is.
    //
    // THE SLOP IS NOT A FUDGE. Correcting every last fraction of a unit makes
    // a resting body jitter: it is pushed out, gravity pulls it back in, and
    // it buzzes forever. Leaving a sliver of overlap alone lets it settle, and
    // the sliver is far too small to see.
    //
    // AND THE FRACTION IS NOT ONE for the same reason. Moving them exactly
    // flush means the next step starts with them a hair apart, so the contact
    // is gone and gravity re-establishes it - which is a contact that enters
    // and exits forever rather than one that stays.
    constexpr float kSlop     = 0.01f;
    constexpr float kFraction = 0.8f;

    const float depth = std::max(-contact.separation - kSlop, 0.0f);
    if (depth <= 0.0f) {
        return;
    }

    const Vec2 correction = contact.normal * (depth * kFraction / invMassSum);
    if (bodyA != nullptr) { bodyA->Translate(correction * invMassA); }
    if (bodyB != nullptr) { bodyB->Translate(-correction * invMassB); }
}

} // namespace

void CollisionSystem::Update(float /*deltaSeconds*/) {
    Scene* scene = Scene::Active();
    if (scene == nullptr) {
        return;
    }

    g_thisFrame.clear();
    g_contacts.clear();
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

        live.push_back(Live{collider, record.owner, static_cast<int>(g_slices.size())});
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

            const Pair pair = MakePair(live[i].owner, live[j].owner);
            g_thisFrame.insert(pair);

            // Stored once, oriented for whichever entity comes first in the
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

            // Keyed so that `first` of the pair is the one `info` describes,
            // which is what lets the dispatcher know when to flip.
            if (pair.first != live[i].owner) {
                info.other         = live[i].owner;
                info.otherCollider = live[i].collider;
                info.thisCollider  = live[j].collider;
                info.normal        = deepest.normal;
                info.relativeVelocity = velA - velB;
            }
            g_contacts.emplace(pair, info);
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

    // ONCE, after the velocities have settled.
    for (auto& [pair, contact] : g_contacts) {
        if (solid(contact)) {
            CorrectPosition(contact, contact.thisCollider, contact.otherCollider);
        }
    }

    // ---- the events -------------------------------------------------------
    //
    // Compare this step's pairs against last step's. BOTH entities get every
    // event, each naming the other as the partner.
    std::set<Pair> triggersThisFrame;

    for (const Pair& pair : g_thisFrame) {
        const auto contactIt = g_contacts.find(pair);
        const bool isTrigger =
            contactIt != g_contacts.end() && contactIt->second.thisCollider != nullptr &&
            contactIt->second.otherCollider != nullptr &&
            (contactIt->second.thisCollider->IsTrigger() ||
             contactIt->second.otherCollider->IsTrigger());

        if (isTrigger) {
            triggersThisFrame.insert(pair);
        }

        const bool wasTouching = g_lastFrame.contains(pair);
        const char* type =
            isTrigger ? (wasTouching ? MessageTypes::kTriggerStay
                                     : MessageTypes::kTriggerEnter)
                      : (wasTouching ? MessageTypes::kCollisionStay
                                     : MessageTypes::kCollisionEnter);

        SendPairEvent(type, pair.first, pair.second);
        SendPairEvent(type, pair.second, pair.first);
    }

    for (const Pair& pair : g_lastFrame) {
        if (g_thisFrame.contains(pair)) {
            continue;
        }
        // Exit: it was touching last step and is not now. Sent as the same
        // KIND of event the enter was.
        const char* type = g_lastFrameTriggers.contains(pair) ? MessageTypes::kTriggerExit
                                                              : MessageTypes::kCollisionExit;
        SendPairEvent(type, pair.first, pair.second);
        SendPairEvent(type, pair.second, pair.first);
    }

    g_lastFrame         = g_thisFrame;
    g_lastFrameTriggers = triggersThisFrame;

    // ---- gizmos -----------------------------------------------------------
    //
    // Draw every collider, so you can SEE what the collision system thinks the
    // shapes are. Green is not touching anything, red is touching something,
    // cyan is a trigger, grey is disabled. The Scene view's Gizmos menu
    // switches the whole Colliders category off.
    if (Gizmos::IsCategoryEnabled(GizmoCategory::Colliders)) {
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
            } else {
                const bool touching =
                    std::any_of(g_thisFrame.begin(), g_thisFrame.end(),
                                [&](const Pair& pair) {
                                    return pair.first == record.owner ||
                                           pair.second == record.owner;
                                });
                if (touching) {
                    color = Color::Red();
                }
            }

            // The same buffer the gather above used, refilled. It still has
            // the capacity it reached there, so the gizmo pass allocates
            // nothing at all.
            const int count = GatherWorldShapes(*collider, scratch);
            for (int i = 0; i < count; ++i) {
                DrawShapeGizmo(scratch[i], color);
            }
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
