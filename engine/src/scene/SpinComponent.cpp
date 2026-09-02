// =============================================================================
//  SpinComponent.cpp - A SHELL. The declarations are real; the bodies are
//  yours.
//
//  This is the SMALLEST complete component in the engine, which makes it the
//  best one to write first: a value read from the scene file, a registration
//  with a system, and one line of behaviour per step. Everything else in
//  scene/ is that same shape, larger.
//
//  SpinComponent.h explains what each piece is for.
// =============================================================================

#include <engine/scene/SpinComponent.h>

namespace eng {

// TODO: take this component back out of the spin system's list. A component
// that unregisters in its DESTRUCTOR is already half torn down; see
// Component.h for why OnDetach exists instead.
SpinComponent::~SpinComponent() {}

// TODO: read "degreesPerSecond" out of the scene file. Everything a component
// can be configured with reaches it through here.
bool SpinComponent::Deserialize(const Json& /*node*/, std::string& outError) {
    outError.clear();
    return true;
}

bool SpinComponent::Serialize(Json& /*out*/) const { return true; }

void SpinComponent::OnAttach() {}
void SpinComponent::OnDetach() {}

// ---------------------------------------------------------------------------
//  SpinSystem - stage 300 (Movement)
// ---------------------------------------------------------------------------
void SpinSystem::Register(SpinComponent& /*spin*/) {}

void SpinSystem::Unregister(SpinComponent& /*spin*/) {}

void SpinSystem::Clear() {}

std::size_t SpinSystem::Count() { return 0; }

// TODO: turn every registered component by its own rate, multiplied by
// deltaSeconds. Multiply by dt rather than assuming a frame rate - that is the
// whole reason the step is handed in.
void SpinSystem::Update(float /*deltaSeconds*/) {}

// TODO: tell the component factory that "SpinComponent" means this class, so
// a scene file can ask for one.
void SpinSystem::RegisterComponentTypes() {}

} // namespace eng
