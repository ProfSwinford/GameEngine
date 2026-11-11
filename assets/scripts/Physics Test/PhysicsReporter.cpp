// ============================================================================
//  PhysicsReporter - makes collision and trigger EVENTS visible.
//
//  A collider outline tells you what shape the physics system thinks something
//  is. It does not tell you whether the events fired, how many times, with
//  what normal, or whether the exit ever arrived. Those are the things that go
//  wrong quietly, and this script draws all of them.
//
//  ==========================================================================
//  WHAT IT DRAWS, above its own entity:
//
//    C 3 in 2 out, 1 now     collision enters and exits since the scene
//    T 1 in 1 out, 0 now     started, and how many pairs are live right now
//
//  THE THIRD NUMBER IS THE ONE THAT CATCHES BUGS. Enters minus exits is how
//  many pairs the script believes it is in, and it has to come back to zero
//  when everything has rolled away. A count that drifts upward means exits
//  are being missed; one that goes negative means they are being sent twice.
//
//  and at each contact, for a second after it happens:
//
//    red dot      where the two met
//    yellow line  the contact normal, which ALWAYS POINTS AT THIS ENTITY -
//                 see Contact.h. A landing has a normal pointing up; walking
//                 into a wall has one pointing sideways. If a normal here
//                 points the wrong way, that is a real defect.
//    orange line  the relative velocity the pair had before it was solved,
//                 scaled down. A hard landing draws a long one.
//
//  ==========================================================================
//  BOTH FAMILIES ARE DEFINED ON PURPOSE.
//
//  A pair of colliders sends collision events or trigger events, never both,
//  and a script with only the wrong family never fires - the Console says so,
//  which is how you find out. Defining both means one script can be dropped on
//  anything in the playground, solid or trigger, and will report whatever that
//  thing really gets. An entity carrying BOTH kinds of collider gets both, and
//  then the two lines above tell them apart.
// ============================================================================

#include <engine/Engine.h>

#include <cstdio>


class PhysicsReporter final : public eng::ScriptBehaviour {
public:
    // How long a contact marker stays on screen. The event itself happens in
    // one step and is over before you can look at it.
    float markerSeconds = 1.0f;   // [0..5] 0 shows it for a single frame

    bool showCounts  = true;    // the running totals above the entity
    bool showNormals = true;    // the markers at each contact
    bool logToConsole = false;  // one line per event, which gets loud fast

    // Where the counts sit relative to the entity, in world units.
    eng::Vec2 countsOffset{0.0f, 34.0f};

    void OnUpdate() {
        if (!showCounts || transform == nullptr) {
            return;
        }

        const int solidNow   = m_collisionEnter - m_collisionExit;
        const int triggerNow = m_triggerEnter - m_triggerExit;

        char line[128];
        std::snprintf(line, sizeof(line), "C %d in %d out, %d now   T %d in %d out, %d now",
                      m_collisionEnter, m_collisionExit, solidNow,
                      m_triggerEnter, m_triggerExit, triggerNow);

        // Red while something is actually touching, so a glance says whether
        // the pair is live rather than historical.
        const bool touching = solidNow > 0 || triggerNow > 0;

        eng::Gizmos::Text(transform->WorldPosition() + countsOffset, line,
                          touching ? eng::Color::Red() : eng::Color::Grey(), 0.0f,
                          eng::GizmoSpace::World);
    }

    // ---- solid contacts ---------------------------------------------------

    void OnCollisionEnter(const eng::Collision2D& hit) {
        ++m_collisionEnter;
        Mark(hit, eng::Color::Red());
        Log("collision enter", hit);
    }

    void OnCollisionStay(const eng::Collision2D& hit) {
        // The normal only, and only for this frame. A stay fires every step,
        // so a one-second marker per step would pile a hundred of them up.
        if (showNormals) {
            eng::Gizmos::Line(hit.point, hit.point + hit.normal * 18.0f,
                              eng::Color{200, 200, 90, 255});
        }
    }

    void OnCollisionExit(eng::EntityId other) {
        ++m_collisionExit;
        // NO CONTACT ON AN EXIT, by design - the pair has stopped touching, so
        // there is nothing to describe. That is why this hook takes the id
        // form and the other two do not. See Contact.h.
        if (logToConsole) {
            ENGINE_LOG_INFO(eng::Channels::kGame, "{}: collision exit with {}",
                            Name(), Describe(other));
        }
    }

    // ---- triggers ---------------------------------------------------------

    void OnTriggerEnter(const eng::Collision2D& hit) {
        ++m_triggerEnter;
        Mark(hit, eng::Color::Cyan());
        Log("trigger enter", hit);
    }

    void OnTriggerStay(const eng::Collision2D& hit) {
        if (showNormals) {
            eng::Gizmos::Line(hit.point, hit.point + hit.normal * 18.0f,
                              eng::Color{90, 200, 200, 255});
        }
    }

    void OnTriggerExit(eng::EntityId other) {
        ++m_triggerExit;
        if (logToConsole) {
            ENGINE_LOG_INFO(eng::Channels::kGame, "{}: trigger exit with {}", Name(),
                            Describe(other));
        }
    }

private:
    void Mark(const eng::Collision2D& hit, eng::Color colour) const {
        if (!showNormals) {
            return;
        }
        eng::Gizmos::Circle(hit.point, 4.0f, colour, markerSeconds);
        eng::Gizmos::Line(hit.point, hit.point + hit.normal * 28.0f,
                          eng::Color::Yellow(), markerSeconds);

        // Scaled right down: a body landing at 900 units a second would
        // otherwise draw a line across four stations.
        eng::Gizmos::Line(hit.point, hit.point + hit.relativeVelocity * 0.05f,
                          eng::Color::Orange(), markerSeconds);
    }

    void Log(const char* what, const eng::Collision2D& hit) const {
        if (!logToConsole) {
            return;
        }
        ENGINE_LOG_INFO(eng::Channels::kGame,
                        "{}: {} with {} - normal ({:.2f}, {:.2f}), separation {:.2f}, "
                        "closing at {:.0f}",
                        Name(), what, Describe(hit.other), hit.normal.x, hit.normal.y,
                        hit.separation, hit.relativeVelocity.Length());
    }

    // Looked up fresh, because an id is all that survives a step and the
    // entity it names may already have been destroyed.
    std::string Describe(eng::EntityId other) const {
        if (scene == nullptr) {
            return "<no scene>";
        }
        const eng::Entity* partner = scene->Get(other);
        return partner != nullptr ? partner->Name() : "<already gone>";
    }

    int m_collisionEnter = 0;
    int m_collisionExit  = 0;
    int m_triggerEnter   = 0;
    int m_triggerExit    = 0;
};


ENGINE_REGISTER_SCRIPT(PhysicsReporter)
