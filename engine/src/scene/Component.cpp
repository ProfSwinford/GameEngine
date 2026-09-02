// =============================================================================
//  Component.cpp - A SHELL. The declarations are real; the bodies are yours.
//
//  Everything here compiles and links, so the editor builds and runs from day
//  one. It just does not do this part yet: each function returns a harmless
//  neutral value so nothing crashes and nothing pretends to have worked.
//
//  Fill these in as the course reaches them. Component.h explains WHAT each
//  function is for and WHY it exists - read it first.
// =============================================================================

#include <engine/scene/Component.h>

namespace eng {

// ---------------------------------------------------------------------------
//  Component - the questions any component can ask about its owner
// ---------------------------------------------------------------------------
EntityId Component::OwnerId() const { return EntityId{}; }

Scene* Component::GetScene() const { return nullptr; }

Transform2D* Component::OwnerTransform() const { return nullptr; }

// ---------------------------------------------------------------------------
//  ComponentFactory - turning a NAME in a scene file into an object
//
//  TODO: a table of type name -> "how to make one". This is the piece that
//  makes levels DATA rather than code: without it, a scene file's
//  "TransformComponent" is just a string nobody can act on.
// ---------------------------------------------------------------------------
void ComponentFactory::Register(std::string_view /*typeName*/, CreateFn /*create*/) {}

std::unique_ptr<Component> ComponentFactory::Create(std::string_view /*typeName*/) {
    return nullptr;
}

bool ComponentFactory::IsRegistered(std::string_view /*typeName*/) { return false; }

void ComponentFactory::ForEachType(const std::function<void(const char*)>& /*fn*/) {}

// TODO: tell the factory which type names mean which classes. The Inspector's
// "+ ComponentName" buttons are built from this list, so they stay empty until
// it is written.
void ComponentFactory::RegisterBuiltins() {}

// ---------------------------------------------------------------------------
//  TransformComponent
// ---------------------------------------------------------------------------
bool TransformComponent::Deserialize(const Json& /*node*/, std::string& outError) {
    outError.clear();
    return true;
}

bool TransformComponent::Serialize(Json& /*out*/) const { return true; }

// ---------------------------------------------------------------------------
//  SpriteComponent
// ---------------------------------------------------------------------------
SpriteComponent::~SpriteComponent() {}

bool SpriteComponent::Deserialize(const Json& /*node*/, std::string& outError) {
    outError.clear();
    return true;
}

bool SpriteComponent::Serialize(Json& /*out*/) const { return true; }

// TODO: a component REGISTERS ITSELF with the system that draws it, here -
// and takes itself back out in OnDetach. Doing it in the constructor instead
// hands a half-built object to something that may use it immediately; see
// Component.h.
void SpriteComponent::OnAttach() {}
void SpriteComponent::OnDetach() {}

void SpriteComponent::SetTint(Color tint) { m_tint = tint; }

void SpriteComponent::SetLayer(int layer) { m_layer = layer; }

void SpriteComponent::SetTexture(std::string_view virtualPath) {
    m_texturePath = std::string(virtualPath);
}

// ---------------------------------------------------------------------------
//  SpriteRenderSystem
//
//  TODO: keep a list of exactly the sprites that exist, and draw them lowest
//  layer first. Nothing appears in the Scene or Game view until this is
//  written - the views will show a cleared background and nothing else.
// ---------------------------------------------------------------------------
void SpriteRenderSystem::Register(SpriteComponent& /*sprite*/) {}

void SpriteRenderSystem::Unregister(SpriteComponent& /*sprite*/) {}

void SpriteRenderSystem::Render(Camera& /*camera*/) {}

std::size_t SpriteRenderSystem::Count() { return 0; }

void SpriteRenderSystem::Clear() {}

} // namespace eng
