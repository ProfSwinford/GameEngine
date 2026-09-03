// =============================================================================
//  SpinComponent.cpp - a skeleton. Every function is here with the right
//  signature and an empty body. SpinComponent.h is the specification; read it
//  first.
//
//  This is the smallest complete component in the engine, which makes it the
//  best one to write first: a value read from a file, a registration with a
//  system, and one line of behaviour per step. Everything in scene/ is that
//  same shape, larger.
// =============================================================================

#include <engine/scene/SpinComponent.h>

namespace eng {

// A safety net for a component that was built but never attached, which happens
// when a scene fails to load part-way through.
SpinComponent::~SpinComponent() {
}

// Reads the turning rate from the scene file, in either radians or degrees per
// second. Giving both is an authoring mistake and is reported rather than
// quietly resolved.
bool SpinComponent::Deserialize(const Json& /*node*/, std::string& /*outError*/) {
    return false;
}

// Writes the turning rate back out, always in radians.
bool SpinComponent::Serialize(Json& /*out*/) const {
    return false;
}

// Adds this component to the spin system's list.
void SpinComponent::OnAttach() {
}

// Takes it back out. Doing this in the destructor instead would mean
// unregistering an object that is already half torn down.
void SpinComponent::OnDetach() {
}

// Adds a component to the list the system walks.
void SpinSystem::Register(SpinComponent& /*spin*/) {
}

// Takes a component back out of that list.
void SpinSystem::Unregister(SpinComponent& /*spin*/) {
}

// Empties the list, used when a scene is unloaded.
void SpinSystem::Clear() {
}

// How many spinning components exist.
std::size_t SpinSystem::Count() {
    return 0;
}

// Turns every registered component by its own rate. Multiply by deltaSeconds
// rather than assuming a frame rate - that is the whole reason the step is
// handed in rather than looked up.
void SpinSystem::Update(float /*deltaSeconds*/) {
}

// Tells the component factory that "SpinComponent" means this class, so a scene
// file can ask for one.
void SpinSystem::RegisterComponentTypes() {
}

} // namespace eng
