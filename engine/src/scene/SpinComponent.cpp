// ============================================================================
//  SpinComponent.cpp - the spinning component and the system that updates it.
//  See SpinComponent.h.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/math/Transform2D.h>
#include <engine/scene/SpinComponent.h>

#include <algorithm>

namespace eng {
namespace {

// Every spinning component currently attached to something. The system walks
// this list rather than every entity in the scene asking "are you a spinner?".
std::vector<SpinComponent*> g_spins;

} // namespace

SpinComponent::~SpinComponent() {
    // A safety net. OnDetach is where the removal normally happens; this
    // catches the case of a component that was built but never attached, which
    // happens when loading a scene fails partway through.
    SpinSystem::Unregister(*this);
}

bool SpinComponent::Deserialize(const Json& node, std::string& outError) {
    const bool hasRadians = HasKey(node, "radiansPerSecond");
    const bool hasDegrees = HasKey(node, "degreesPerSecond");

    if (hasRadians && hasDegrees) {
        // Reported rather than quietly picking one. A scene file saying two
        // different things about the same value is an authoring mistake, and
        // only the author can say which they meant.
        outError = "SpinComponent gives both radiansPerSecond and degreesPerSecond; "
                   "using radiansPerSecond and ignoring the other";
        m_radiansPerSecond = ReadFloat(node, "radiansPerSecond", 0.0f, kTypeName);
        return false;
    }

    if (hasRadians) {
        m_radiansPerSecond = ReadFloat(node, "radiansPerSecond", 0.0f, kTypeName);
    } else if (hasDegrees) {
        // kDegToRad comes from Vec2.h and is just pi/180.
        m_radiansPerSecond = ReadFloat(node, "degreesPerSecond", 0.0f, kTypeName) * kDegToRad;
    } else {
        outError = "SpinComponent needs either radiansPerSecond or degreesPerSecond";
        return false;
    }

    return true;
}

bool SpinComponent::Serialize(Json& out) const {
    // Always written in radians, which is the field Deserialize prefers when
    // both are present. A file authored in degrees therefore comes back in
    // radians after a save - a small loss of the original wording, and the
    // reason radiansPerSecond is documented as the main one.
    out["radiansPerSecond"] = m_radiansPerSecond;
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

void        SpinSystem::Clear() { g_spins.clear(); }
std::size_t SpinSystem::Count() { return g_spins.size(); }

void SpinSystem::Update(float deltaSeconds) {
    // Walked by index with the size re-read, because in principle a spin could
    // be attached from inside another system's update.
    for (std::size_t i = 0; i < g_spins.size(); ++i) {
        SpinComponent* spin = g_spins[i];
        if (spin == nullptr) {
            continue;
        }
        Transform2D* transform = spin->OwnerTransform();
        if (transform == nullptr) {
            continue;
        }

        // deltaSeconds is the FIXED step, handed down by the scheduler. Asking
        // a clock for the elapsed time here instead is what would make the
        // simulation behave differently at different frame rates.
        transform->Rotate(spin->RadiansPerSecond() * deltaSeconds);
    }
}

void SpinSystem::RegisterComponentTypes() {
    ComponentFactory::Register(SpinComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<SpinComponent>();
                               });
}

} // namespace eng
