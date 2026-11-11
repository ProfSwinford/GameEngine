// ============================================================================
//  PhysicsProbe - one world query, fired every step and drawn where it went.
//
//  THIS IS THE STATION THAT FINDS QUERY BUGS, and it finds them by sweeping.
//  A query fired once in a fixed direction either hits or does not, and both
//  answers look plausible. A query that TURNS slowly, with its hit point and
//  its surface normal drawn, shows the moment the answer stops making sense:
//  a normal that flips halfway along a flat wall, a hit that jumps to the far
//  side of a circle, a cast that stops a radius short on one side and not the
//  other. You see it happen rather than having to think of the case.
//
//  ==========================================================================
//  WHAT IS DRAWN
//
//    white      the query itself - the ray, or the shape being swept
//    green      no hit
//    red        the hit point
//    yellow     the surface normal at the hit, 24 units long
//    magenta    the bounds of every collider an overlap query found
//
//  ==========================================================================
//  ONE QUERY PER PROBE, chosen by `mode`.
//
//  Not all of them at once, deliberately. Five overlapping drawings in one
//  place tell you nothing; five probes side by side, each doing one thing, let
//  you compare them. The playground puts one of each in a row.
// ============================================================================

#include <engine/Engine.h>


class PhysicsProbe final : public eng::ScriptBehaviour {
public:
    // Which query to fire:
    //   0  Raycast          the nearest thing along a line
    //   1  RaycastAll       everything along that line
    //   2  CircleCast       a circle slid along the line - a thick ray
    //   3  BoxCast          a box slid along the line
    //   4  OverlapCircle    everything inside a circle, no direction at all
    //   5  OverlapBox       everything inside a rotated box
    //   6  OverlapPoint     whatever is under one point
    int mode = 0;   // [0..6] 0 ray, 1 rayAll, 2 circleCast, 3 boxCast, 4 overlapCircle, 5 overlapBox, 6 point

    float length = 260.0f;   // how far a ray or a cast reaches, in world units
    float radius = 20.0f;    // the circle, for CircleCast and OverlapCircle

    eng::Vec2 boxSize{48.0f, 32.0f};   // the box, for BoxCast and OverlapBox

    // THE AIM, in degrees, measured anticlockwise from "right". Turned every
    // step by the speed below, which is what makes this a sweep rather than a
    // single sample.
    float degrees = 0.0f;              // [-360..360] which way it points now
    float degreesPerSecond = 35.0f;    // [-360..360] 0 holds still

    // ---- the filter, which is the other half of what can go wrong ---------

    // The layer to look for. "All" means every layer, and that is the default
    // so a probe that has never been configured finds things rather than
    // silently finding nothing.
    std::string layer = "All";   // a collision layer name, or "All"

    bool hitTriggers = true;   // can this query see trigger colliders?
    bool ignoreSelf  = true;   // skip the entity this probe is on

    void OnFixedUpdate() {
        if (transform == nullptr) {
            return;
        }

        degrees += degreesPerSecond * eng::Time::FixedDeltaTime();
        if (degrees > 360.0f)  { degrees -= 720.0f; }
        if (degrees < -360.0f) { degrees += 720.0f; }

        const float    radians = degrees * eng::kDegToRad;
        const eng::Vec2 origin = transform->WorldPosition();
        const eng::Vec2 aim{std::cos(radians), std::sin(radians)};

        // REBUILT EVERY STEP rather than cached, so editing `layer` or
        // unticking `hitTriggers` in the Inspector takes effect at once.
        eng::QueryFilter filter;
        filter.layer       = layer.empty() ? eng::kCollisionLayerAll : layer;
        filter.hitTriggers = hitTriggers;
        if (ignoreSelf) {
            filter.ignore = OwnerId();
        }

        switch (mode) {
            case 0: DrawCast(eng::Physics2D::Raycast(origin, aim, length, filter),
                             origin, aim, 0.0f); break;
            case 1: DrawRaycastAll(origin, aim, filter); break;
            case 2: DrawCast(eng::Physics2D::CircleCast(origin, radius, aim, length,
                                                        filter),
                             origin, aim, radius); break;
            case 3: DrawBoxCast(origin, aim, radians, filter); break;
            case 4: DrawOverlapCircle(origin, filter); break;
            case 5: DrawOverlapBox(origin, radians, filter); break;
            default: DrawOverlapPoint(origin, filter); break;
        }
    }

private:
    static constexpr int kMaxHits = 16;

    // The hit point, its normal, and how far along the query reached.
    //
    // `castRadius` is drawn as a circle at the stopping point for a circle
    // cast, because "where did it stop" and "where did it touch" are different
    // places for a thick ray and conflating them is exactly the bug this
    // station is here to show.
    void DrawCast(const eng::RaycastHit2D& hit, eng::Vec2 origin, eng::Vec2 aim,
                  float castRadius) const {
        const eng::Vec2 end = origin + aim * (hit ? hit.distance : length);

        eng::Gizmos::Line(origin, origin + aim * length,
                          hit ? eng::Color::White() : eng::Color::Green());

        if (castRadius > 0.0f) {
            eng::Gizmos::Circle(end, castRadius, eng::Color{120, 120, 200, 255});
        }
        if (!hit) {
            return;
        }

        eng::Gizmos::Circle(hit.point, 4.0f, eng::Color::Red());
        eng::Gizmos::Line(hit.point, hit.point + hit.normal * 24.0f,
                          eng::Color::Yellow());
    }

    // EVERY hit, so you can see whether they really arrive nearest first and
    // whether the far ones are there at all. Each is numbered.
    void DrawRaycastAll(eng::Vec2 origin, eng::Vec2 aim,
                        const eng::QueryFilter& filter) const {
        eng::RaycastHit2D hits[kMaxHits];
        const int count = eng::Physics2D::RaycastAll(origin, aim, length, hits,
                                                     kMaxHits, filter);

        eng::Gizmos::Line(origin, origin + aim * length,
                          count > 0 ? eng::Color::White() : eng::Color::Green());

        for (int i = 0; i < count; ++i) {
            eng::Gizmos::Circle(hits[i].point, 4.0f, eng::Color::Red());
            eng::Gizmos::Line(hits[i].point, hits[i].point + hits[i].normal * 20.0f,
                              eng::Color::Yellow());
            eng::Gizmos::Text(hits[i].point + eng::Vec2{6.0f, 6.0f},
                              std::to_string(i), eng::Color::Red(), 0.0f,
                              eng::GizmoSpace::World);
        }
    }

    void DrawBoxCast(eng::Vec2 origin, eng::Vec2 aim, float radians,
                     const eng::QueryFilter& filter) const {
        const eng::RaycastHit2D hit =
            eng::Physics2D::BoxCast(origin, boxSize, radians, aim, length, filter);

        eng::Gizmos::Line(origin, origin + aim * length,
                          hit ? eng::Color::White() : eng::Color::Green());

        // THE BOX WHERE IT STOPPED, through the same transform the query used,
        // so the drawing cannot disagree with the test.
        const eng::Vec2 end = origin + aim * (hit ? hit.distance : length);
        eng::Gizmos::TransformedBox(
            eng::Mat3::FromTRS(end, radians, eng::Vec2{1.0f, 1.0f}), boxSize * 0.5f,
            eng::Color{120, 120, 200, 255}, 0.0f, eng::GizmoCategory::Default);

        if (hit) {
            eng::Gizmos::Circle(hit.point, 4.0f, eng::Color::Red());
            eng::Gizmos::Line(hit.point, hit.point + hit.normal * 24.0f,
                              eng::Color::Yellow());
        }
    }

    void DrawOverlapCircle(eng::Vec2 origin, const eng::QueryFilter& filter) const {
        eng::ColliderComponent* found[kMaxHits] = {};
        const int count =
            eng::Physics2D::OverlapCircle(origin, radius, found, kMaxHits, filter);

        eng::Gizmos::Circle(origin, radius,
                            count > 0 ? eng::Color::White() : eng::Color::Green());
        MarkFound(found, count);
    }

    void DrawOverlapBox(eng::Vec2 origin, float radians,
                        const eng::QueryFilter& filter) const {
        eng::ColliderComponent* found[kMaxHits] = {};
        const int count = eng::Physics2D::OverlapBox(origin, boxSize, radians, found,
                                                     kMaxHits, filter);

        eng::Gizmos::TransformedBox(
            eng::Mat3::FromTRS(origin, radians, eng::Vec2{1.0f, 1.0f}),
            boxSize * 0.5f, count > 0 ? eng::Color::White() : eng::Color::Green(),
            0.0f, eng::GizmoCategory::Default);
        MarkFound(found, count);
    }

    void DrawOverlapPoint(eng::Vec2 origin, const eng::QueryFilter& filter) const {
        eng::ColliderComponent* hit = eng::Physics2D::OverlapPoint(origin, filter);
        eng::Gizmos::Circle(origin, 3.0f,
                            hit != nullptr ? eng::Color::White() : eng::Color::Green());
        if (hit != nullptr) {
            MarkFound(&hit, 1);
        }
    }

    // The bounds of each collider an overlap query returned. WorldBounds is
    // built from the collider's own world shapes, so a box drawn here that
    // does not line up with the green outline means the two disagree - which
    // is a real defect and not a drawing mistake.
    static void MarkFound(eng::ColliderComponent* const* found, int count) {
        for (int i = 0; i < count; ++i) {
            if (found[i] != nullptr) {
                eng::Gizmos::Box(found[i]->WorldBounds(), eng::Color::Magenta());
            }
        }
    }
};


ENGINE_REGISTER_SCRIPT(PhysicsProbe)
