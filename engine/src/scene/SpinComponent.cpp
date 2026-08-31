// WEEK 6 - the spin component and its system. See SpinComponent.h for why a
// single "rotate myself" field is enough to produce a three-deep orbit.

#include <engine/core/Log.h>
#include <engine/math/Transform2D.h>
#include <engine/scene/SpinComponent.h>

#include <algorithm>

namespace eng {
namespace {

// Dense array of the components to update, same shape as SpriteRecord's for
// the same reason - see the AoS/SoA note in Component.h.
std::vector<SpinComponent*> g_spins;

} // namespace

StringId SpinComponent::TypeIdStatic() {
    static const StringId id = Intern(kTypeName);
    return id;
}

SpinComponent::~SpinComponent() {
    // Safety net for a component destroyed without ever being attached, which
    // happens when Deserialize fails during a scene load. OnDetach is the
    // mechanism; this is the backstop.
    SpinSystem::Unregister(*this);
}

bool SpinComponent::Deserialize(const ConfigNode& node, std::string& outError) {
    const ConfigNode radians = node.Child("radiansPerSecond");
    const ConfigNode degrees = node.Child("degreesPerSecond");

    if (radians.IsValid() && degrees.IsValid()) {
        // Reported, not silently resolved. A scene file that says two
        // different things about one value is an authoring mistake, and the
        // author is the only one who can decide which they meant.
        outError = node.Path() +
                   " sets both radiansPerSecond and degreesPerSecond; using "
                   "radiansPerSecond and ignoring the other";
        m_radiansPerSecond = static_cast<f32>(radians.AsFloat(0.0));
        return false;
    }

    if (radians.IsValid()) {
        m_radiansPerSecond = static_cast<f32>(radians.AsFloat(0.0));
    } else if (degrees.IsValid()) {
        m_radiansPerSecond = static_cast<f32>(degrees.AsFloat(0.0)) * kDegToRad;
    } else {
        outError = node.Path() +
                   " needs either radiansPerSecond or degreesPerSecond (a spin of zero "
                   "is legal but is almost certainly not what was meant)";
        return false;
    }
    return true;
}

bool SpinComponent::Serialize(ConfigWriter& out) const {
    // Written as radians, which is the field Deserialize prefers when both are
    // present. Round-tripping a file authored in degrees therefore converts it
    // to radians - a real, if small, loss of authoring intent, and the reason
    // the header documents radiansPerSecond as the canonical one.
    out.SetFloat("radiansPerSecond", static_cast<f64>(m_radiansPerSecond));
    return true;
}

void SpinComponent::OnAttach() {
    SpinSystem::Register(*this);
}

void SpinComponent::OnDetach() {
    SpinSystem::Unregister(*this);
}

void SpinSystem::Register(SpinComponent& spin) {
    g_spins.push_back(&spin);
}

void SpinSystem::Unregister(SpinComponent& spin) {
    std::erase(g_spins, &spin);
}

void SpinSystem::Clear() {
    g_spins.clear();
}

usize SpinSystem::Count() {
    return g_spins.size();
}

void SpinSystem::Update(f32 deltaSeconds) {
    // Iterating by index and re-reading the size, because a spin could in
    // principle be attached from another system's update. Nothing does that
    // today; the loop costs nothing and the alternative is a rule somebody has
    // to remember.
    for (usize i = 0; i < g_spins.size(); ++i) {
        SpinComponent* spin = g_spins[i];
        if (spin == nullptr) {
            continue;
        }
        Transform2D* transform = spin->OwnerTransform();
        if (transform == nullptr) {
            continue;
        }
        // deltaSeconds is the FIXED step, handed down by the scheduler. Reading
        // a clock here instead is what makes a simulation frame-rate dependent,
        // and it is the thing Week 10's whole design exists to prevent.
        transform->Rotate(spin->RadiansPerSecond() * deltaSeconds);
    }
}

void SpinSystem::RegisterComponentTypes() {
    ComponentFactory::Register(SpinComponent::kTypeName, []() -> std::unique_ptr<Component> {
        return std::make_unique<SpinComponent>();
    });
}

} // namespace eng
