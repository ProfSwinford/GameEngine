// =============================================================================
//  Component.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. Component.h is the specification; read it first.
// =============================================================================

#include <engine/scene/Component.h>

namespace eng {

// The id of the entity this component is attached to.
EntityId Component::OwnerId() const {
    return EntityId{};
}

// The scene the owning entity lives in.
Scene* Component::GetScene() const {
    return nullptr;
}

// The owning entity's transform - the thing most components need and the reason
// this shortcut exists.
Transform2D* Component::OwnerTransform() const {
    return nullptr;
}

// Records that a type name means a particular class, so a scene file can ask
// for one by name. This is what makes levels data instead of code.
void ComponentFactory::Register(std::string_view /*typeName*/, CreateFn /*create*/) {
}

// Builds one component from its type name, or nullptr when nothing has claimed
// that name.
std::unique_ptr<Component> ComponentFactory::Create(std::string_view /*typeName*/) {
    return nullptr;
}

// Has anything claimed this type name?
bool ComponentFactory::IsRegistered(std::string_view /*typeName*/) {
    return false;
}

// Lists every registered type name, which is what fills the Inspector's
// "+ ComponentName" buttons without anybody maintaining a second list.
void ComponentFactory::ForEachType(const std::function<void(const char*)>& /*fn*/) {
}

// Registers the component types the engine ships with. Until this runs, the
// names in a scene file mean nothing.
void ComponentFactory::RegisterBuiltins() {
}

// Reads position, rotation and scale from the scene file. Every field is
// optional, so a file can give a position and leave the rest alone.
bool TransformComponent::Deserialize(const Json& /*node*/, std::string& /*outError*/) {
    return false;
}

// Writes those three back out, using the SAME key names Deserialize reads -
// which is what makes load, edit, save, load give back what you had.
bool TransformComponent::Serialize(Json& /*out*/) const {
    return false;
}

// Lets go of this sprite's share of the picture. If it was the last one using
// that image, the image unloads itself.
SpriteComponent::~SpriteComponent() {
}

// Reads the texture path, tint, layer and size from the scene file.
bool SpriteComponent::Deserialize(const Json& /*node*/, std::string& /*outError*/) {
    return false;
}

// Writes those back out.
bool SpriteComponent::Serialize(Json& /*out*/) const {
    return false;
}

// Loads the picture and adds this sprite to the render system's list. A
// component registers itself HERE, not in its constructor - see Component.h.
void SpriteComponent::OnAttach() {
}

// Takes this sprite back out of the render system's list.
void SpriteComponent::OnDetach() {
}

// Changes the colour the picture is multiplied by.
void SpriteComponent::SetTint(Color /*tint*/) {
}

// Changes which layer the sprite draws on. Higher numbers draw on top.
void SpriteComponent::SetLayer(int /*layer*/) {
}

// Swaps the picture for a different file, loading it if nothing else has.
void SpriteComponent::SetTexture(std::string_view /*virtualPath*/) {
}

// Adds a sprite to the list the render system walks.
void SpriteRenderSystem::Register(SpriteComponent& /*sprite*/) {
}

// Takes a sprite back out of that list.
void SpriteRenderSystem::Unregister(SpriteComponent& /*sprite*/) {
}

// Draws every registered sprite through the given camera, lowest layer first.
// Nothing appears in either view until this is written.
void SpriteRenderSystem::Render(Camera& /*camera*/) {
}

// How many sprites are currently registered.
std::size_t SpriteRenderSystem::Count() {
    return 0;
}

// Empties the list, used when a scene is unloaded.
void SpriteRenderSystem::Clear() {
}

} // namespace eng
