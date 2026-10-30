#pragma once

// ============================================================================
//  EntityRef.h - one entity pointing at another, from the Inspector.
//
//  A script declares a reference, drags an entity onto it in the Inspector,
//  and reaches whatever it needs on the other side:
//
//      class Turret final : public eng::ScriptBehaviour {
//      public:
//          eng::EntityRef target;
//
//          void OnExpose(eng::ScriptFields& fields) {
//              fields.Ref("target", target, "Rigidbody2DComponent",
//                         "what to shoot at");
//          }
//
//          void OnFixedUpdate() {
//              if (eng::Entity* enemy = target.Target()) {
//                  AimAt(enemy->Transform().WorldPosition());
//              }
//          }
//      };
//
//  ==========================================================================
//  IT STORES A NAME. NOT A POINTER, AND NOT AN EntityId.
//
//  NOT A POINTER, because the thing it refers to can be destroyed while the
//  game runs - a pickup is collected, an enemy dies - and anything still
//  holding a raw pointer is then reading memory that has been reused. That is
//  the whole argument of EntityId.h, and a field somebody set in the editor is
//  not exempt from it.
//
//  NOT AN EntityId EITHER, which is the surprising half. An id is index +
//  generation, and both are decided by the ORDER A SCENE HAPPENS TO LOAD IN.
//  Save "slot 7, generation 1" into a level file, insert an entity above it in
//  the file, and that id now refers to something else entirely - silently, and
//  with no way for anything to notice. An id is the right handle for the
//  lifetime of one run and the wrong one to write down.
//
//  A NAME survives being written to a file, is what the Hierarchy shows you,
//  is what you would type if you were editing the scene by hand, and is
//  already indexed - Scene::Find is a hash lookup, not a search. Scene names
//  are kept unique, which is what makes the reference unambiguous.
//
//  ==========================================================================
//  IT RESOLVES EVERY TIME IT IS ASKED, and does not cache.
//
//  A cached id would be wrong in exactly the case that matters: the entity is
//  destroyed, its slot is reused by something else, and the cache now points
//  at a stranger with the same index. The lookup is a hash of a short string
//  against a map the scene already maintains; a script asking sixty times a
//  second is not something to optimise before it shows up in a profile.
//
//  So the rule is the engine's usual one, and it applies here too: ask for the
//  entity when you need it, use it, and do not keep it.
//
//  ==========================================================================
//  WHAT A BROKEN REFERENCE DOES
//
//  Returns null, and says so in the Inspector. An entity that was deleted, or
//  renamed out from under a reference, leaves the NAME in place - so the field
//  still reads "Player" in red rather than going quietly blank, and whoever
//  looks at it can see what it used to point at.
//
//  Renaming from the Hierarchy does not break anything: Scene::RenameEntity
//  moves every reference across with it. See the note there for the one case
//  it cannot reach.
// ============================================================================

#include <engine/scene/Entity.h>
#include <engine/scene/EntityId.h>

#include <string>
#include <string_view>

namespace eng {

class Scene;

class EntityRef {
public:
    EntityRef() = default;
    explicit EntityRef(std::string_view targetName) : m_name(targetName) {}

    // ---- what it points at -----------------------------------------------

    // The name as stored, whether or not anything answers to it. KEPT when the
    // target goes missing, because "this used to point at Player" is the one
    // piece of information that makes a broken reference fixable.
    const std::string& TargetName() const { return m_name; }

    bool IsSet() const { return !m_name.empty(); }

    void Set(std::string_view targetName) { m_name.assign(targetName); }
    void Set(const Entity& entity) { m_name = entity.Name(); }
    void Clear() { m_name.clear(); }

    // ---- getting there ---------------------------------------------------

    // The entity, or null when nothing in the active scene has that name -
    // which covers "not set yet", "deleted" and "renamed" without the caller
    // having to tell them apart.
    Entity* Target() const;

    // Its id, for sending it a message or storing it for one frame.
    EntityId Id() const;

    // A COMPONENT on it, which is the usual reason for having a reference at
    // all. Same spelling as Entity::Find<T>, because it is the same question
    // asked one step further away:
    //
    //     if (auto* body = target.Find<eng::Rigidbody2DComponent>()) { ... }
    //
    // This works for every component the ENGINE defines. For a reference to
    // another SCRIPT, see FindScriptOn in ScriptComponent.h - it cannot live
    // here, and the note there says why.
    template <class T>
    T* Find() const {
        Entity* entity = Target();
        return entity != nullptr ? entity->Find<T>() : nullptr;
    }

    // `if (target)` - true only when it currently resolves, so it answers the
    // question callers actually have. Use IsSet() to ask the other one:
    // whether a name was ever filled in.
    explicit operator bool() const { return Target() != nullptr; }

    friend bool operator==(const EntityRef& a, const EntityRef& b) {
        return a.m_name == b.m_name;
    }

private:
    std::string m_name;
};

// ---------------------------------------------------------------------------
//  Does this entity carry a component or a script with this name?
//
//  ONE QUESTION ACROSS BOTH KINDS OF ATTACHMENT, which is what lets a
//  reference be constrained in the Inspector without the engine caring
//  whether "Rigidbody2DComponent" is built in and "DoorSwitch" is a script
//  somebody wrote this afternoon. Both are registered under a name - see
//  ComponentFactory and ScriptRegistry - and from here they look the same.
//
//  A component type name is checked first, then a script name. A script that
//  happens to share a built-in component's name would be shadowed by it, and
//  that is the stable choice rather than the other way round: the built-in
//  list cannot change under a project, and a script can be renamed.
// ---------------------------------------------------------------------------
bool EntityHasNamed(const Entity& entity, std::string_view componentOrScriptName);

// Is this a name anything could have? Used to catch a constraint that is a
// typo or an explanatory sentence rather than a type - see ScriptFields::Ref.
//
// FALSE DOES NOT MEAN WRONG. A script that is not compiled into this build is
// not registered, so a perfectly correct constraint naming it answers false
// here. Every caller treats this as "worth mentioning", never as an error.
bool IsKnownComponentOrScriptName(std::string_view componentOrScriptName);

} // namespace eng
