// ============================================================================
//  Rigidbody.cpp - the component and the system that integrates it. See
//  Rigidbody.h for the body types and the order of one step.
//
//  THE WHOLE FILE WORKS IN INVERSE MASS. Every push is divided by mass, so
//  storing 1/m instead means an immovable body is 0 rather than infinity -
//  and multiplying by zero needs no branch, no special case and no guard.
//  That single choice is why the contact solver has no "if this one is
//  static" anywhere in it.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/physics/Collider.h>
#include <engine/physics/Physics2D.h>
#include <engine/physics/Rigidbody.h>
#include <engine/scene/Entity.h>
#include <engine/scene/Scene.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace eng {
namespace {

std::vector<Rigidbody2DComponent*> g_bodies;


std::size_t g_awakeCount = 0;

} // namespace

// ---------------------------------------------------------------------------
//  Reading and writing
// ---------------------------------------------------------------------------

bool Rigidbody2DComponent::Deserialize(const Json& node, std::string& outError) {
    const std::string type = ReadString(node, "bodyType", "Dynamic", kTypeName);
    if (type == "Dynamic") {
        m_bodyType = RigidbodyType::Dynamic;
    } else if (type == "Kinematic") {
        m_bodyType = RigidbodyType::Kinematic;
    } else if (type == "Static") {
        m_bodyType = RigidbodyType::Static;
    } else {
        outError = "\"bodyType\" should be \"Dynamic\", \"Kinematic\" or \"Static\"";
        return false;
    }

    SetMass(ReadFloat(node, "mass", m_mass, kTypeName));
    m_useAutoMass = ReadBool(node, "useAutoMass", m_useAutoMass, kTypeName);

    SetLinearDrag(ReadFloat(node, "linearDrag", m_linearDrag, kTypeName));
    SetAngularDrag(ReadFloat(node, "angularDrag", m_angularDrag, kTypeName));

    m_gravityScale   = ReadFloat(node, "gravityScale", m_gravityScale, kTypeName);
    m_freezeRotation = ReadBool(node, "freezeRotation", m_freezeRotation, kTypeName);

    m_velocity        = ReadVec2(node, "velocity", m_velocity, kTypeName);
    m_angularVelocity = ReadFloat(node, "angularVelocity", m_angularVelocity, kTypeName);

    outError.clear();
    return true;
}

bool Rigidbody2DComponent::Serialize(Json& out) const {
    switch (m_bodyType) {
        case RigidbodyType::Dynamic:   out["bodyType"] = "Dynamic";   break;
        case RigidbodyType::Kinematic: out["bodyType"] = "Kinematic"; break;
        case RigidbodyType::Static:    out["bodyType"] = "Static";    break;
    }

    out["mass"]         = m_mass;
    out["gravityScale"] = m_gravityScale;

    // Only written when not the default, so a plain dynamic body is four
    // readable lines rather than ten.
    if (m_useAutoMass)         { out["useAutoMass"] = m_useAutoMass; }
    if (m_linearDrag != 0.0f)  { out["linearDrag"] = m_linearDrag; }
    if (m_angularDrag != 0.05f) { out["angularDrag"] = m_angularDrag; }
    if (m_freezeRotation)      { out["freezeRotation"] = m_freezeRotation; }

    // The starting velocity is part of the scene; the velocity it happens to
    // have mid-play is not something a saved scene should bake in. The editor
    // snapshots play mode separately, which is where the live value belongs.
    if (m_velocity.x != 0.0f || m_velocity.y != 0.0f) {
        WriteVec2(out, "velocity", m_velocity);
    }

    // AND THE SPIN, which Deserialize has always read and this has never
    // written. A scene with a spinning body saved it stopped, and in the
    // editor every Play then Stop went through the same snapshot - so the
    // spin a scene was authored with was destroyed by pressing play once,
    // while the linear velocity beside it survived.
    if (m_angularVelocity != 0.0f) {
        out["angularVelocity"] = m_angularVelocity;
    }
    return true;
}

void Rigidbody2DComponent::OnAttach() {
    RigidbodySystem::Register(*this);
}

void Rigidbody2DComponent::OnDetach() {
    RigidbodySystem::Unregister(*this);
}

// ---------------------------------------------------------------------------
//  Mass
// ---------------------------------------------------------------------------

void Rigidbody2DComponent::SetBodyType(RigidbodyType type) {
    m_bodyType = type;

    // A body that has just stopped being dynamic should not carry its old
    // motion into its new life as scenery.
    if (type != RigidbodyType::Dynamic) {
        m_velocity        = Vec2{0.0f, 0.0f};
        m_angularVelocity = 0.0f;
    }
    ClearAccumulators();
}

void Rigidbody2DComponent::SetMass(float mass) {
    // A massless dynamic body would be infinitely easy to push, and 1/m is
    // what the solver multiplies by.
    m_mass = std::max(mass, 0.0001f);
}

float Rigidbody2DComponent::Mass() const {
    if (!m_useAutoMass) {
        return m_mass;
    }

    // Worked out from the collider: area times density, the way Unity's
    // automatic mass does. An entity whose only collider has no area - an
    // edge, which is a line - falls back to the stated mass.
    Entity* owner = Owner();
    if (owner == nullptr) {
        return m_mass;
    }

    // ======================================================================
    //  EACH COLLIDER'S OWN AREA TIMES ITS OWN DENSITY, SUMMED.
    //
    //  It used to sum the areas and multiply the total by whichever density
    //  the LAST collider happened to carry. So a compound body of a 100-area
    //  collider at density 1 and another at density 5 weighed 1000 or 200
    //  depending purely on which order the two components were attached in -
    //  a five-fold swing in mass from nothing at all, which also silently
    //  re-weighted every contact the body took part in. Unity sums per
    //  collider, which gives 600 either way round.
    //
    //  AND A DISABLED COLLIDER IS LEFT OUT. "Switched off, a collider keeps
    //  all its settings and takes part in nothing" is what Collider.h
    //  promises, and an attack hitbox that is live for three frames used to
    //  make its body permanently seven times harder to push.
    // ======================================================================
    float mass = 0.0f;
    for (std::size_t i = 0; i < owner->ComponentCount(); ++i) {
        const auto* collider = dynamic_cast<ColliderComponent*>(owner->ComponentAt(i));
        if (collider == nullptr || !collider->Enabled()) {
            continue;
        }
        mass += collider->Area() * collider->Density();
    }
    if (mass <= 0.0f) {
        return m_mass;
    }
    return std::max(mass, 0.0001f);
}

float Rigidbody2DComponent::InverseMass() const {
    // ZERO FOR ANYTHING THAT IS NOT DYNAMIC. See the note at the top of this
    // file: this is the one place "immovable" is decided, and every other
    // piece of the solver gets it for free.
    if (m_bodyType != RigidbodyType::Dynamic) {
        return 0.0f;
    }
    return 1.0f / Mass();
}

float Rigidbody2DComponent::InverseInertia() const {
    if (m_bodyType != RigidbodyType::Dynamic || m_freezeRotation) {
        return 0.0f;
    }

    Entity* owner = Owner();
    if (owner == nullptr) {
        return 0.0f;
    }

    // Taken from the collider's own SIZE rather than from its exact shape.
    //
    // The moment of inertia of a rectangle is m(w^2 + h^2)/12, and using the
    // box around whatever the real shape is overestimates it a little - so
    // things are slightly harder to spin than they strictly should be. That
    // errs toward calm, which is the right direction for a number whose only
    // job is to make a box tumble convincingly.
    // THE FIRST LIVE ONE. It used to take the first collider of any kind, so
    // a body whose first component was a switched-off hitbox took its whole
    // rotational response from a shape that was taking part in nothing.
    const ColliderComponent* found = nullptr;
    for (std::size_t i = 0; i < owner->ComponentCount(); ++i) {
        const auto* collider = dynamic_cast<const ColliderComponent*>(
            owner->ComponentAt(i));
        if (collider != nullptr && collider->Enabled()) {
            found = collider;
            break;
        }
    }
    if (found == nullptr) {
        return 0.0f;   // nothing to spin
    }

    // ======================================================================
    //  THE COLLIDER'S OWN SIZE TIMES THE WORLD SCALE, AND NOT ITS WORLD
    //  BOUNDS.
    //
    //  World bounds are the UPRIGHT box around the rotated shape, so they
    //  grow and shrink as the body turns: a 100x100 box measures 100x100 at
    //  zero degrees and 141x141 at forty-five. Its inertia therefore doubled
    //  and halved once per revolution, which is not a property any rigid body
    //  has. A tumbling crate visibly changed how hard it was to spin as it
    //  went round.
    //
    //  LocalSize is rotation-free by construction, so multiplying by the
    //  world scale gives the same answer at every angle - while still
    //  tracking an entity that is scaled up or parented under something that
    //  is.
    // ======================================================================
    const Vec2 scale = owner->Transform().WorldScale();
    const Vec2 local = found->LocalSize();
    const Vec2 size{local.x * std::fabs(scale.x), local.y * std::fabs(scale.y)};

    float inertia = Mass() * (size.x * size.x + size.y * size.y) / 12.0f;

    // ======================================================================
    //  AND THE OFFSET COUNTS, which it did not.
    //
    //  A body turns about its TRANSFORM ORIGIN, not about its collider. A
    //  collider offset away from that origin is correspondingly harder to
    //  swing round it - that is the parallel axis theorem, I = I_centre + m
    //  d^2, and it is one line. Without it a character whose collider sits at
    //  its feet span like a collider at its middle, which is to say far too
    //  easily.
    // ======================================================================
    const Vec2 offset{found->Offset().x * scale.x, found->Offset().y * scale.y};
    inertia += Mass() * offset.LengthSquared();

    if (inertia <= 0.0f) {
        return 0.0f;
    }
    return 1.0f / inertia;
}

// ---------------------------------------------------------------------------
//  The knobs
// ---------------------------------------------------------------------------

void Rigidbody2DComponent::SetAngularVelocity(float radiansPerSecond) {
    m_angularVelocity = radiansPerSecond;
}

void Rigidbody2DComponent::SetLinearDrag(float drag) {
    m_linearDrag = std::max(drag, 0.0f);
}

void Rigidbody2DComponent::SetAngularDrag(float drag) {
    m_angularDrag = std::max(drag, 0.0f);
}

// ---------------------------------------------------------------------------
//  Pushing it
// ---------------------------------------------------------------------------

void Rigidbody2DComponent::AddForce(Vec2 force, ForceMode mode) {
    if (m_bodyType != RigidbodyType::Dynamic) {
        return;   // pushing a wall is allowed and does nothing
    }

    if (mode == ForceMode::Force) {
        m_force += force;
        return;
    }

    // An impulse is a change in momentum, so it becomes a change in velocity
    // immediately rather than waiting for the integration.
    m_velocity += force * InverseMass();
}

void Rigidbody2DComponent::AddForceAtPosition(Vec2 force, Vec2 worldPosition,
                                              ForceMode mode) {
    if (m_bodyType != RigidbodyType::Dynamic) {
        return;
    }

    AddForce(force, mode);

    const Transform2D* transform = OwnerTransform();
    if (transform == nullptr) {
        return;
    }

    // The part of an off-centre push that becomes spin. Cross(arm, force) in
    // 2D is a single number, which is exactly what torque is here.
    const Vec2  arm    = worldPosition - transform->WorldPosition();
    const float torque = Cross(arm, force);
    AddTorque(torque, mode);
}

void Rigidbody2DComponent::AddTorque(float torque, ForceMode mode) {
    if (m_bodyType != RigidbodyType::Dynamic || m_freezeRotation) {
        return;
    }
    if (mode == ForceMode::Force) {
        m_torque += torque;
        return;
    }
    m_angularVelocity += torque * InverseInertia();
}

void Rigidbody2DComponent::MovePosition(Vec2 worldPosition) {
    m_hasMoveTarget = true;
    m_moveTarget    = worldPosition;
}

void Rigidbody2DComponent::MoveRotation(float radians) {
    m_hasRotateTarget = true;
    m_rotateTarget    = radians;
}

Vec2 Rigidbody2DComponent::VelocityAtPoint(Vec2 worldPosition) const {
    const Transform2D* transform = OwnerTransform();
    if (transform == nullptr || m_angularVelocity == 0.0f) {
        return m_velocity;
    }

    // Spinning adds a sideways velocity that grows with distance from the
    // centre: w x r, which in 2D is the arm turned a quarter turn and scaled.
    const Vec2 arm = worldPosition - transform->WorldPosition();
    return m_velocity + arm.Perpendicular() * m_angularVelocity;
}

void Rigidbody2DComponent::AddVelocity(Vec2 delta) {
    // STATIC IS STILL STATIC. The solver never asks, because SolverMasses
    // gives a static body zero - but this is the one place "immovable" is
    // decided for velocity, and it should not depend on the caller.
    if (m_bodyType == RigidbodyType::Static) {
        return;
    }
    m_velocity += delta;
}

void Rigidbody2DComponent::ApplyImpulse(Vec2 impulse, Vec2 contactPoint) {
    if (m_bodyType != RigidbodyType::Dynamic) {
        return;
    }

    m_velocity += impulse * InverseMass();

    const Transform2D* transform = OwnerTransform();
    if (transform == nullptr || m_freezeRotation) {
        return;
    }
    const Vec2 arm = contactPoint - transform->WorldPosition();
    m_angularVelocity += Cross(arm, impulse) * InverseInertia();
}

void Rigidbody2DComponent::Translate(Vec2 delta) {
    // A WORLD-SPACE DELTA, because the only caller is the contact solver and
    // a contact is in world space. Transform2D::Translate adds to the LOCAL
    // position, so this went wrong by the parent's scale - see the note in
    // IntegrateVelocity, which had the same bug on the same line.
    Transform2D* transform = OwnerTransform();
    if (transform != nullptr) {
        transform->TranslateWorld(delta);
    }
}

void Rigidbody2DComponent::ClearAccumulators() {
    m_force  = Vec2{0.0f, 0.0f};
    m_torque = 0.0f;
}

// ---------------------------------------------------------------------------
//  Integration
// ---------------------------------------------------------------------------

void Rigidbody2DComponent::IntegrateForces(float fixedStep, Vec2 gravity) {
    if (m_bodyType != RigidbodyType::Dynamic) {
        return;
    }

    // GRAVITY IS AN ACCELERATION, not a force, which is why it is not divided
    // by mass. That is the whole of Galileo: a heavy thing and a light thing
    // fall at the same rate, and writing it this way makes that automatic
    // rather than something to remember.
    m_velocity += (gravity * m_gravityScale + m_force * InverseMass()) * fixedStep;

    // DRAG AS A DIVISION, not a subtraction.
    //
    // `v -= v * drag * dt` is the obvious spelling and it is unstable: a large
    // drag or a long step makes the subtraction overshoot past zero and the
    // body accelerates backwards. Dividing can never change the sign however
    // big the numbers get. Box2D does it this way, and so therefore does
    // Unity 2D.
    if (m_linearDrag > 0.0f) {
        m_velocity *= 1.0f / (1.0f + fixedStep * m_linearDrag);
    }

    if (!m_freezeRotation) {
        m_angularVelocity += m_torque * InverseInertia() * fixedStep;
        if (m_angularDrag > 0.0f) {
            m_angularVelocity *= 1.0f / (1.0f + fixedStep * m_angularDrag);
        }
    } else {
        m_angularVelocity = 0.0f;
    }
}

void Rigidbody2DComponent::IntegrateVelocity(float fixedStep) {
    if (m_bodyType == RigidbodyType::Static) {
        return;
    }

    Transform2D* transform = OwnerTransform();
    if (transform == nullptr) {
        return;
    }

    // ======================================================================
    //  LAST STEP'S CATCH-UP VELOCITY IS SPENT.
    //
    //  MovePosition means "be there by the END OF THIS STEP". The velocity
    //  that gets there is set below, and it has to survive into stage 400 so
    //  the contact solver can shove whatever the platform is pushing - that
    //  is the whole reason this is a velocity and not a teleport.
    //
    //  But it has no meaning once that step is over, and it used to be left
    //  in place. So ONE call to MovePosition did not move a body once: it
    //  moved it at that speed forever. A lift told to go up by 200 units went
    //  up by 200 units every step until it left the level. Scripts that call
    //  it every step - which is how it is meant to be used, and what the
    //  playground does - never noticed.
    //
    //  KINEMATIC ONLY. A dynamic body has already had this step's gravity
    //  added by IntegrateForces, and wiping that would make MovePosition on a
    //  dynamic body cancel gravity rather than merely fight the solver.
    // ======================================================================
    //  AND ONLY IF IT IS STILL THE CATCH-UP VELOCITY. A script is free to
    //  call MovePosition once and switch to SetVelocity the next step -
    //  scripts run at stage 200 and this at 350, in the same step - and
    //  wiping unconditionally threw that away: exactly one step of motion
    //  lost, silently, with Velocity() reading zero for the rest of the
    //  frame. So the value the target produced is remembered and compared.
    if (m_bodyType == RigidbodyType::Kinematic) {
        if (m_spentMoveTarget && !m_hasMoveTarget && m_velocity == m_spentVelocity) {
            m_velocity = Vec2{0.0f, 0.0f};
        }
        if (m_spentRotateTarget && !m_hasRotateTarget &&
            m_angularVelocity == m_spentAngularVelocity) {
            m_angularVelocity = 0.0f;
        }
    }
    m_spentMoveTarget   = false;
    m_spentRotateTarget = false;

    // MovePosition turns into whatever velocity gets there in one step, rather
    // than into a teleport. A teleport passes through things; a velocity
    // pushes them, which is what makes a moving platform carry what is
    // standing on it. See the note in Rigidbody.h.
    if (m_hasMoveTarget) {
        m_hasMoveTarget   = false;
        m_spentMoveTarget = true;
        if (fixedStep > 0.0f) {
            m_velocity = (m_moveTarget - transform->WorldPosition()) * (1.0f / fixedStep);
        }
        m_spentVelocity = m_velocity;
    }
    if (m_hasRotateTarget) {
        m_hasRotateTarget   = false;
        m_spentRotateTarget = true;
        if (fixedStep > 0.0f && !m_freezeRotation) {
            m_angularVelocity =
                (m_rotateTarget - transform->WorldRotation()) * (1.0f / fixedStep);
        }
        m_spentAngularVelocity = m_angularVelocity;
    }

    // ======================================================================
    //  THROUGH THE WORLD, NOT THROUGH THE PARENT'S SPACE.
    //
    //  m_velocity is in world units: gravity is, every impulse the solver
    //  applies is, and MovePosition is documented as taking a WORLD position.
    //  Transform2D::Translate adds to the LOCAL position, which is in the
    //  parent's space - so under a parent scaled (2, 2) this moved the body
    //  twice as far as its velocity said.
    //
    //  The body then accelerated through the world at twice gravity while
    //  reporting the right velocity, and the contact solver - which works
    //  entirely in world space - was resolving a body whose reported velocity
    //  was not the one it had. Parenting is a first-class editor gesture and
    //  prefab roots are routinely scaled, so this was not an exotic case.
    // ======================================================================
    transform->TranslateWorld(m_velocity * fixedStep);
    if (!m_freezeRotation) {
        transform->Rotate(m_angularVelocity * fixedStep);
    }
}

// ---------------------------------------------------------------------------
//  RigidbodySystem
// ---------------------------------------------------------------------------

void RigidbodySystem::Register(Rigidbody2DComponent& body) {
    if (std::find(g_bodies.begin(), g_bodies.end(), &body) == g_bodies.end()) {
        g_bodies.push_back(&body);
    }
}

void RigidbodySystem::Unregister(Rigidbody2DComponent& body) {
    const auto it = std::find(g_bodies.begin(), g_bodies.end(), &body);
    if (it == g_bodies.end()) {
        return;
    }
    *it = g_bodies.back();
    g_bodies.pop_back();
}

void RigidbodySystem::Clear() {
    g_bodies.clear();
    g_awakeCount = 0;
}

std::size_t RigidbodySystem::Count()      { return g_bodies.size(); }
std::size_t RigidbodySystem::AwakeCount() { return g_awakeCount; }

void RigidbodySystem::Update(float deltaSeconds) {

    g_awakeCount  = 0;

    const Vec2 gravity = Physics2D::Gravity();

    // FORCES FIRST FOR EVERY BODY, THEN POSITIONS FOR EVERY BODY.
    //
    // Two passes rather than one, so that no body moves while another is still
    // working out its velocity. With a single pass, the first body in the list
    // would be integrated against this step's gravity and the last against a
    // world where several things had already moved - and the result would
    // depend on the order components happened to be attached in.
    // A SWITCHED-OFF BODY IS NOT INTEGRATED AND KEEPS ITS VELOCITY.
    //
    // Keeping it is the choice worth stating. Zeroing it would mean switching
    // something off and on again brought it back stopped, which is not what
    // anybody means by switching something off - and SetBodyType already
    // destroys velocity, so there is a spelling for that when it IS wanted.
    // See Entity::IsActiveInHierarchy.
    for (Rigidbody2DComponent* body : g_bodies) {
        if (!body->OwnerIsActive()) {
            continue;
        }
        body->IntegrateForces(deltaSeconds, gravity);
    }
    for (Rigidbody2DComponent* body : g_bodies) {
        if (!body->OwnerIsActive()) {
            continue;
        }
        body->IntegrateVelocity(deltaSeconds);

        if (body->BodyType() == RigidbodyType::Dynamic &&
            (body->Velocity().LengthSquared() > 1e-6f ||
             std::fabs(body->AngularVelocity()) > 1e-6f)) {
            ++g_awakeCount;
        }
    }

    // CLEARED AT THE END OF THE STEP, as in Unity: AddForce means "for this
    // step". A script that wants a continuous push applies it every step,
    // which is why that call belongs in OnFixedUpdate.
    for (Rigidbody2DComponent* body : g_bodies) {
        body->ClearAccumulators();
    }
}

void RigidbodySystem::RegisterComponentTypes() {
    ComponentFactory::Register(Rigidbody2DComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<Rigidbody2DComponent>();
                               });
}

} // namespace eng
