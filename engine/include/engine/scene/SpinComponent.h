#pragma once

// =============================================================================
//  WEEK 6 - the component that makes Milestone 1 actually demonstrable.
//
//  M1 requires that "a three-deep parented hierarchy ORBITS and ROTATES
//  correctly" and that panning and zooming leave it visually correct. A scene
//  file can describe the hierarchy, but nothing in the engine was moving it -
//  `orbit_test.json` rendered 22 sprites that sat perfectly still, and the
//  milestone was being claimed on the strength of the transform unit tests
//  alone. Those tests are real evidence and they are not the same thing as the
//  check the milestone asks for.
//
//  This is the smallest component that closes that gap: it adds
//  `radiansPerSecond * dt` to its own transform's LOCAL rotation, every
//  simulation step.
//
//  ---------------------------------------------------------------------------
//  WHY THAT ONE FIELD IS ENOUGH TO PRODUCE AN ORBIT.
//
//  There is no orbit code here, and there does not need to be. Spinning a
//  PARENT sweeps every child around it, because a child's world matrix is
//  `local * parentWorld` - which is the entire point of the transform
//  hierarchy and exactly what Week 6 built.
//
//  So in `orbit_test.json`:
//    SolarRoot spins  ->  Planet (its child, offset 160 units) ORBITS the origin
//    Planet spins     ->  Moon (its child, offset 50 units) ORBITS the Planet,
//                         AND the Planet visibly rotates
//    Moon spins       ->  the Moon rotates on its own axis, backwards
//
//  Three numbers in a data file produce a three-deep orbiting system, and if
//  the composition order in Mat3 were wrong it would be immediately, visibly
//  wrong on screen rather than subtly wrong in a way you could talk yourself
//  out of.
//
//  ---------------------------------------------------------------------------
//  Component.h says "two component types is enough to prove the model. Resist
//  adding more." That instinct is right and this is the exception it is worth
//  making: without it the milestone's own acceptance criterion cannot be run.
//  It lives in its own file rather than in Component.h so that the "required
//  this week: Transform and Sprite" claim there stays true.
// =============================================================================

#include <engine/scene/Component.h>
#include <engine/scene/SystemOrder.h>

namespace eng {

class SpinComponent final : public Component {
public:
    static constexpr const char* kTypeName = "SpinComponent";
    static StringId TypeIdStatic();

    ~SpinComponent() override;

    StringId    TypeId() const override { return TypeIdStatic(); }
    const char* TypeName() const override { return kTypeName; }

    // Scene-file fields:
    //   "radiansPerSecond": 0.6     - signed; negative spins clockwise
    //   "degreesPerSecond": 34.4    - the same thing in the unit a human
    //                                 actually thinks in. If both are given,
    //                                 radiansPerSecond wins and the conflict
    //                                 is reported rather than silently
    //                                 resolved.
    bool Deserialize(const ConfigNode& node, std::string& outError) override;
    bool Serialize(ConfigWriter& out) const override;

    void OnAttach() override;
    void OnDetach() override;

    f32  RadiansPerSecond() const { return m_radiansPerSecond; }
    void SetRadiansPerSecond(f32 rate) { m_radiansPerSecond = rate; }

private:
    f32 m_radiansPerSecond = 0.0f;
};

// Updates every attached SpinComponent. Registered at SystemStage::kMovement,
// which is stage 300 - BEFORE collision at 400, so colliders are tested at the
// positions things actually moved to this tick rather than last tick's. That
// is the one ordering pair SystemOrder.h calls out by name, and this is the
// first system in the engine for which it matters.
class SpinSystem final : public System {
public:
    void        Update(f32 deltaSeconds) override;
    const char* Name() const override { return "SpinSystem"; }
    i32         Order() const override { return SystemStage::kMovement; }

    static void Register(SpinComponent& spin);
    static void Unregister(SpinComponent& spin);
    static void Clear();
    static usize Count();

    static void RegisterComponentTypes();
};

} // namespace eng
