// ============================================================================
//  EntityRef.cpp - resolving a reference. See EntityRef.h for why it stores a
//  name rather than a pointer or an id, and why it does not cache.
// ============================================================================

#include <engine/scene/Entity.h>
#include <engine/scene/EntityRef.h>
#include <engine/scene/Scene.h>
#include <engine/scene/ScriptComponent.h>

#include <string_view>

namespace eng {

Entity* EntityRef::Target() const {
    if (m_name.empty()) {
        return nullptr;
    }

    // THE ACTIVE SCENE, not a scene remembered when the reference was set.
    // A reference is a piece of level data, and the level it belongs to is the
    // one that is running - the same reasoning ScriptSystem's collision
    // dispatcher uses to find the entity a message was addressed to.
    Scene* scene = Scene::Active();
    if (scene == nullptr) {
        return nullptr;
    }

    // Two steps, and the second one is not redundant. Find() gives an id from
    // the name index, and Get() checks that id is still live - so an index
    // entry left behind by a destroyed entity cannot hand back a stale
    // pointer. See EntityId.h for what the generation is doing.
    return scene->Get(scene->Find(m_name));
}

EntityId EntityRef::Id() const {
    const Entity* entity = Target();
    return entity != nullptr ? entity->Id() : EntityId{};
}

// ---------------------------------------------------------------------------
//  "Does it have one of those?", asked without caring which kind it is
// ---------------------------------------------------------------------------

bool EntityHasNamed(const Entity& entity, std::string_view componentOrScriptName) {
    if (componentOrScriptName.empty()) {
        return true;   // no constraint: anything satisfies it
    }

    if (entity.FindComponent(componentOrScriptName) != nullptr) {
        return true;
    }

    // ----------------------------------------------------------------------
    //  A SCRIPT IS NOT FOUND BY COMPONENT TYPE NAME, and that is the whole
    //  reason this function exists rather than a call to FindComponent.
    //
    //  Every script on every entity is a ScriptComponent - that is its type
    //  name - so asking FindComponent for "DoorSwitch" finds nothing however
    //  many DoorSwitches are attached. The name that tells one script from
    //  another is inside the component.
    //
    //  WALKED RATHER THAN ASKED FOR THE FIRST, because an entity may carry
    //  several scripts and the one being looked for may be any of them.
    // ----------------------------------------------------------------------
    for (std::size_t i = 0; i < entity.ComponentCount(); ++i) {
        const Component* component = entity.ComponentAt(i);
        if (component == nullptr) {
            continue;
        }
        // Compared as TEXT, because the two string literals live in different
        // binaries - engine.dll and the compiled scripts - and nothing
        // promises the linker folded them to one address. Same care
        // ScriptSystem::SubscribeToCollisions takes.
        if (std::string_view(component->TypeName()) != ScriptComponent::kTypeName) {
            continue;
        }
        if (static_cast<const ScriptComponent*>(component)->ScriptName() ==
            componentOrScriptName) {
            return true;
        }
    }
    return false;
}

bool IsKnownComponentOrScriptName(std::string_view componentOrScriptName) {
    if (componentOrScriptName.empty()) {
        return false;
    }
    return ComponentFactory::IsRegistered(componentOrScriptName) ||
           ScriptRegistry::IsRegistered(componentOrScriptName);
}

} // namespace eng
