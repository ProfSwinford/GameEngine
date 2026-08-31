#pragma once

// ============================================================================
//  SpinComponent.h - makes an entity turn on the spot, forever.
//
//  It adds `radiansPerSecond * deltaSeconds` to its own transform's rotation
//  every simulation step. That is the entire component.
//
//  WHY ONE FIELD IS ENOUGH TO PRODUCE A WHOLE ORBITING SOLAR SYSTEM
//  There is no orbit code here, and none is needed. Spinning a PARENT sweeps
//  everything attached to it around in a circle, because a child's position in
//  the world is worked out through its parent's transform. That is what the
//  parent/child transform tree in Transform2D.h buys.
//
//  So in assets/scenes/orbit_test.json:
//     the root spins   -> the planet (its child, offset sideways) ORBITS the centre
//     the planet spins -> the moon (its child) orbits the planet, and the
//                         planet visibly turns as well
//     the moon spins   -> the moon turns on its own axis
//
//  Three numbers in a data file produce a three-deep orbiting system, with no
//  code written for any of it.
//
//  It is also a good first component to copy when writing your own: it is
//  small enough to read in one go and it shows every piece - the type name,
//  loading and saving, attach and detach, and the system that updates it.
// ============================================================================

#include <engine/scene/Component.h>
#include <engine/scene/SystemOrder.h>

namespace eng {

class SpinComponent final : public Component {
public:
    static constexpr const char* kTypeName = "SpinComponent";

    ~SpinComponent() override;

    const char* TypeName() const override { return kTypeName; }

    // Scene file fields - give ONE of these:
    //   "radiansPerSecond": 0.6     negative turns the other way
    //   "degreesPerSecond": 34.4    the same thing in the unit people think in
    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;

    void OnAttach() override;
    void OnDetach() override;

    float RadiansPerSecond() const { return m_radiansPerSecond; }
    void  SetRadiansPerSecond(float rate) { m_radiansPerSecond = rate; }

private:
    float m_radiansPerSecond = 0.0f;
};

// Turns every attached SpinComponent, once per simulation step.
//
// It runs at stage 300 (Movement), which is BEFORE collision at stage 400 - so
// collisions are checked at the positions things actually moved to this tick
// rather than where they were last tick. That is the ordering pair
// SystemOrder.h calls out by name.
class SpinSystem final : public System {
public:
    void        Update(float deltaSeconds) override;
    const char* Name() const override  { return "SpinSystem"; }
    int         Order() const override { return SystemStage::kMovement; }

    static void        Register(SpinComponent& spin);
    static void        Unregister(SpinComponent& spin);
    static void        Clear();
    static std::size_t Count();

    // Tells the ComponentFactory that "SpinComponent" in a scene file means
    // this class.
    static void RegisterComponentTypes();
};

} // namespace eng
