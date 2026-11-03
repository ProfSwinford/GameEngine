#pragma once

// ============================================================================
//  Rigidbody.h - the component that makes something move by itself.
//
//  A collider says what shape something is. A RIGIDBODY says that it has mass
//  and velocity, and that the physics step is allowed to move it. Those are
//  separate components for the same reason they are in Unity: a wall needs a
//  shape and must never move, and a trigger volume needs a shape and has no
//  physical presence at all.
//
//  ==========================================================================
//  WITHOUT ONE OF THESE, NOTHING IS PUSHED.
//
//  Two solid colliders with no rigidbody between them are both immovable
//  scenery. They still report OnCollisionEnter - knowing that two static
//  things overlap is useful while building a level - but neither moves,
//  because neither has anything that could be moved.
//
//  That is Unity's rule and it is worth stating plainly, because "my player
//  walks through walls" is almost always this: a collider on the player, and
//  no rigidbody.
//
//  ==========================================================================
//  THE THREE BODY TYPES, and picking the wrong one is the usual mistake
//
//    DYNAMIC    Gravity pulls it, forces push it, collisions bounce it. The
//               physics step owns its position - a script that writes
//               Transform()->SetPosition every step is fighting the solver
//               and will win unevenly, which looks like stuttering.
//               Move it with velocity or AddForce.
//
//    KINEMATIC  Moves only when told to, and pushes dynamic things out of its
//               way without being pushed back. Infinite mass, in effect. This
//               is what a moving platform, a lift or a sliding door is - and
//               it is usually what a character controller should be, because
//               a player that can be shoved by its own bullets is rarely
//               wanted.
//
//               GRAVITY DOES NOT PULL IT, which follows from "moves only when
//               told to" and is the thing people meet first: a kinematic
//               platformer character does not fall until its script makes it.
//
//               BUT STATIC SCENERY STOPS IT. Against anything that cannot
//               move at all - static bodies, a tilemap collider, a collider
//               with no rigidbody - the kinematic body is the only thing in
//               the contact that CAN move, so it is the thing that gives way:
//               the velocity heading into the surface goes and the velocity
//               along it survives, so it slides rather than sticking.
//
//               Box2D and Unity pass through in that case. This engine does
//               not, because the paragraph above recommends a kinematic body
//               for a character controller and a character controller that
//               walks through walls is not one. See SolverMasses in
//               physics/Collider.cpp.
//
//               Two KINEMATIC bodies still pass through each other: both
//               would give way, and which one moves is a question for the
//               scripts positioning them rather than for the solver.
//
//    STATIC     Never moves at all. The same as having no rigidbody, said out
//               loud, and the right choice for scenery that wants a material
//               (bounciness, friction) without any motion.
//
//  ==========================================================================
//  HOW ONE STEP WORKS - the whole physics step, in order
//
//    1. stage 350  RigidbodySystem: gravity and forces change VELOCITY, then
//                  velocity changes POSITION. (Semi-implicit Euler: the new
//                  velocity is used to move, not the old one. One character
//                  different from the naive version, and far more stable.)
//    2. stage 400  CollisionSystem: find overlaps, then push them apart and
//                  exchange impulses so things bounce and slide.
//    3. stage 500  the events go out: OnCollisionEnter, OnTriggerEnter, ...
//
//  Forces are CLEARED at the end of every step, exactly as in Unity. AddForce
//  means "for this step", so a continuous push has to be applied every step -
//  which is why it belongs in OnFixedUpdate and not in OnUpdate.
// ============================================================================

#include <engine/math/Vec2.h>
#include <engine/scene/Component.h>
#include <engine/scene/SystemOrder.h>

#include <string>

namespace eng {
enum class RigidbodyType { Dynamic, Kinematic, Static };

// How a push is meant: spread over this step, or delivered all at once.
// Unity's ForceMode2D.
enum class ForceMode {
    // Newtons. Scaled by the step length and by 1/mass, so the same call every
    // step gives smooth acceleration. The one to use for thrust, wind, a
    // conveyor.
    Force,

    // An instant change in momentum, not scaled by the step length. The one to
    // use for a jump or an explosion - a single call, a single kick.
    Impulse,
};

// ---------------------------------------------------------------------------
//  The component.
// ---------------------------------------------------------------------------
class Rigidbody2DComponent final : public Component {
public:
    static constexpr const char* kTypeName = "Rigidbody2DComponent";

    const char* TypeName() const override { return kTypeName; }

    // Scene file fields:
    //   "bodyType":      "Dynamic" | "Kinematic" | "Static"
    //   "mass":          1.0
    //   "useAutoMass":   false      works mass out from the collider's area
    //   "linearDrag":    0.0
    //   "angularDrag":   0.05
    //   "gravityScale":  1.0
    //   "freezeRotation": false
    //   "velocity":      [0, 0]
    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;

    void OnAttach() override;
    void OnDetach() override;

    // ---- what kind of body -------------------------------------------------
    RigidbodyType BodyType() const { return m_bodyType; }
    void SetBodyType(RigidbodyType type);

    bool IsDynamic() const { return m_bodyType == RigidbodyType::Dynamic; }

    // ---- mass --------------------------------------------------------------
    //
    // Mass only matters in comparison. A 1 kg crate and a 10 kg crate behave
    // the way you would expect relative to each other; making everything 100x
    // heavier changes nothing at all, because gravity is an acceleration.
    float Mass() const;
    void SetMass(float mass);

    bool UseAutoMass() const { return m_useAutoMass; }
    void SetUseAutoMass(bool automatic) { m_useAutoMass = automatic; }

    // 1/mass, and ZERO for a kinematic or static body.
    //
    // Zero is how "cannot be pushed" is expressed throughout the solver: every
    // impulse is multiplied by it, so an infinite mass needs no special case
    // and no branch. That is the whole reason the solver works in inverse mass
    // rather than mass.
    float InverseMass() const;

    // The rotational equivalent, worked out from the attached collider's size.
    // Zero when rotation is frozen or the body cannot turn.
    float InverseInertia() const;

    // ---- velocity ----------------------------------------------------------
    Vec2 Velocity() const { return m_velocity; }
    void SetVelocity(Vec2 velocity) { m_velocity = velocity; }

    float AngularVelocity() const { return m_angularVelocity; } // radians/sec
    void SetAngularVelocity(float radiansPerSecond);

    // ---- the knobs ---------------------------------------------------------

    // Multiplies the world's gravity for this body alone. 0 makes it float, 2
    // makes it fall twice as hard, and a negative number makes it rise. This
    // is the single most useful number for making a jump feel right.
    float GravityScale() const { return m_gravityScale; }
    void SetGravityScale(float scale) { m_gravityScale = scale; }

    // Air resistance: how quickly motion bleeds away with nothing acting on
    // it. 0 means it never stops.
    float LinearDrag() const { return m_linearDrag; }
    void SetLinearDrag(float drag);

    float AngularDrag() const { return m_angularDrag; }
    void SetAngularDrag(float drag);

    // Stops the body turning while leaving it free to move. Nearly always what
    // a character wants - a capsule that tips over when it bumps a step is not
    // a player, it is a log.
    bool FreezeRotation() const { return m_freezeRotation; }
    void SetFreezeRotation(bool freeze) { m_freezeRotation = freeze; }

    // ---- pushing it --------------------------------------------------------
    //
    // All of these do nothing to a non-dynamic body, deliberately and
    // silently: a script that pushes a wall should not have to check what kind
    // of body the wall has first.

    void AddForce(Vec2 force, ForceMode mode = ForceMode::Force);

    // Off-centre, so it spins the body as well as moving it. `worldPosition`
    // is where the push is applied, in world space.
    void AddForceAtPosition(Vec2 force, Vec2 worldPosition, ForceMode mode = ForceMode::Force);

    void AddTorque(float torque, ForceMode mode = ForceMode::Force);

    // ---- moving a kinematic body ------------------------------------------
    //
    // Sets the position this body should be at by the end of the step, which
    // the solver reaches by giving it the velocity to get there. Writing the
    // transform directly would teleport it - and a teleport does not push
    // anything out of the way, so a moving platform would pass through
    // whatever is standing on it.
    void MovePosition(Vec2 worldPosition);
    void MoveRotation(float radians);

    // How fast a point attached to this body is travelling, spin included.
    // What a collision's relativeVelocity is built from.
    Vec2 VelocityAtPoint(Vec2 worldPosition) const;

    // ---- used by RigidbodySystem and the contact solver -------------------
    void IntegrateForces(float fixedStep, Vec2 gravity);
    void IntegrateVelocity(float fixedStep);
    void ClearAccumulators();

    // Applies an impulse directly, bypassing the force accumulator. One of the
    // contact solver's two ways of changing a body; it divides by THIS body's
    // mass and does nothing to anything that is not Dynamic.
    void ApplyImpulse(Vec2 impulse, Vec2 contactPoint);

    // ======================================================================
    //  THE OTHER ONE: a velocity change the solver has already worked out.
    //
    //  ApplyImpulse divides by this body's inverse mass, and a KINEMATIC
    //  body's is zero - immovable, by construction, which is what makes it
    //  able to shove dynamic things without being shoved back.
    //
    //  But against something that cannot move EITHER - static scenery, a
    //  tilemap, no rigidbody at all - "both immovable" leaves the solver with
    //  nothing to do, and the kinematic body sailed straight through. For a
    //  moving platform that is arguably fine. For a CHARACTER CONTROLLER,
    //  which the note at the top of this file recommends a kinematic body
    //  for, it means walking through walls.
    //
    //  So in that one case the solver decides the kinematic body is the thing
    //  that gives way - see SolverMasses in Collider.cpp - and hands the
    //  answer over already weighted. This is the only way in: scripts move a
    //  kinematic body through SetVelocity and MovePosition, and AddForce still
    //  does nothing to one, which is the promise "moves only when told to".
    //
    //  LINEAR ONLY. A kinematic body's rotation belongs to whatever is driving
    //  it, and a contact has no business spinning a lift.
    // ======================================================================
    void AddVelocity(Vec2 delta);

    // Moves the body without going through velocity - how the solver corrects
    // an overlap that has already happened.
    void Translate(Vec2 delta);

private:
    RigidbodyType m_bodyType = RigidbodyType::Dynamic;

    float m_mass = 1.0f;
    bool m_useAutoMass = false;

    Vec2 m_velocity{0.0f, 0.0f};
    float m_angularVelocity = 0.0f;

    float m_gravityScale = 1.0f;
    float m_linearDrag = 0.0f;
    float m_angularDrag = 0.05f;
    bool m_freezeRotation = false;

    // Reset at the end of every step. See the note at the top of the file.
    Vec2 m_force{0.0f, 0.0f};
    float m_torque = 0.0f;

    // Set by MovePosition/MoveRotation, consumed by the next integration.
    bool m_hasMoveTarget = false;
    Vec2 m_moveTarget{0.0f, 0.0f};
    bool m_hasRotateTarget = false;
    float m_rotateTarget = 0.0f;
};

// ---------------------------------------------------------------------------
//  The system that integrates them.
//
//  Stage 350 (Physics) - AFTER scripts at 200 and movement at 300, so a force
//  a script added this step is included in this step's motion, and BEFORE
//  collision at 400, so the overlap being resolved is the one the body
//  actually moved into.
// ---------------------------------------------------------------------------
class RigidbodySystem final : public System {
public:
    void Update(float deltaSeconds) override;
    const char* Name() const override { return "RigidbodySystem"; }
    int Order() const override { return SystemStage::kPhysics; }

    static void Register(Rigidbody2DComponent& body);
    static void Unregister(Rigidbody2DComponent& body);
    static void Clear();

    static std::size_t Count();
    static std::size_t AwakeCount();

    static void RegisterComponentTypes();
};
} // namespace eng
