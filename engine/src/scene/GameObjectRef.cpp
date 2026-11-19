// ============================================================================
//  GameObjectRef.cpp - resolving one. See GameObjectRef.h for why it exists.
// ============================================================================

#include <engine/scene/GameObjectRef.h>

#include <engine/scene/Entity.h>
#include <engine/scene/Scene.h>

namespace eng {
namespace {

// Returned by Name() and PrefabPath() when the reference is the other kind.
// A function-local static rather than a temporary, so the reference handed
// back outlives the call - these return by const reference to match EntityRef
// and PrefabRef, which a caller may have taken a reference to.
const std::string& Nothing() {
    static const std::string empty;
    return empty;
}

} // namespace

GameObjectRef::GameObjectRef(Entity* entity) {
    Set(entity);
}

GameObjectRef::GameObjectRef(const Entity& entity) {
    SetSceneObject(entity.Name());
}

GameObjectRef GameObjectRef::ToSceneObject(std::string_view name) {
    GameObjectRef ref;
    ref.SetSceneObject(name);
    return ref;
}

GameObjectRef GameObjectRef::ToPrefab(std::string_view virtualPath) {
    GameObjectRef ref;
    ref.SetPrefab(virtualPath);
    return ref;
}

const std::string& GameObjectRef::Name() const {
    return m_kind == Kind::SceneObject ? m_value : Nothing();
}

const std::string& GameObjectRef::PrefabPath() const {
    return m_kind == Kind::Prefab ? m_value : Nothing();
}

Entity* GameObjectRef::Target() const {
    // A PREFAB RESOLVES TO NOTHING, and that is the honest answer rather than
    // a missing feature: it is not in the scene. See the header.
    if (m_kind != Kind::SceneObject || m_value.empty()) {
        return nullptr;
    }

    // THE ACTIVE SCENE, not one remembered when the reference was set - the
    // same argument EntityRef::Target makes, and the same two steps for the
    // same reason: Find() gives an id from the name index and Get() checks it
    // is still live, so an index entry left behind by a destroyed entity
    // cannot hand back a stale pointer.
    Scene* scene = Scene::Active();
    if (scene == nullptr) {
        return nullptr;
    }
    return scene->Get(scene->Find(m_value));
}

EntityId GameObjectRef::Id() const {
    const Entity* entity = Target();
    return entity != nullptr ? entity->Id() : EntityId{};
}

void GameObjectRef::Clear() {
    m_kind = Kind::None;
    m_value.clear();
}

void GameObjectRef::Set(Entity* entity) {
    if (entity == nullptr) {
        Clear();
        return;
    }
    SetSceneObject(entity->Name());
}

void GameObjectRef::SetSceneObject(std::string_view name) {
    if (name.empty()) {
        Clear();
        return;
    }
    m_kind  = Kind::SceneObject;
    m_value = std::string(name);
}

void GameObjectRef::SetPrefab(std::string_view virtualPath) {
    if (virtualPath.empty()) {
        Clear();
        return;
    }
    m_kind  = Kind::Prefab;
    m_value = std::string(virtualPath);
}

} // namespace eng
