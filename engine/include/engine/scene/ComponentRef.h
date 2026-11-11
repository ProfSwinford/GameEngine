#pragma once

// ============================================================================
//  ComponentRef.h - a script pointing at a COMPONENT on another entity.
//
//  Unity's object fields, with Unity's spelling. A script declares one, you
//  drag an entity from the Hierarchy onto it in the Inspector, and the script
//  reaches the component without knowing or caring where it lives:
//
//      class Follower final : public MyScriptBehaviour {
//      public:
//          TransformRef   anchor;   // drag the thing to follow here
//          SpriteRenderer flash;    // and the sprite to tint here
//
//          void OnFixedUpdate() {
//              if (anchor) {
//                  transform->SetWorldPosition(anchor->WorldPosition());
//              }
//          }
//      };
//
//  `TransformRef`, `SpriteRenderer` and `Rigidbody2D` are aliases for this
//  type in Engine.h; `eng::ComponentRef<eng::TilemapComponent>` is the long
//  form, and works for every CONCRETE component the engine defines.
//
//  It is TransformRef and not `Transform` because it cannot be `Transform`:
//  ScriptBehaviour declares a member function of that name, and inside a
//  script class that hides a namespace-scope type of the same spelling. The
//  long note in Engine.h has the whole story; it predates this header.
//
//  And there is no reference to a collider, because ColliderComponent is an
//  abstract base with no kTypeName and Entity::Find looks components up by
//  exactly that. Name the concrete one - BoxColliderComponent and friends.
//
//  ==========================================================================
//  WHY THIS IS NOT A POINTER, which is the first thing anybody asks
//
//  Because a pointer could not be filled in. The Inspector matters in EDIT
//  mode, and in edit mode the clock is paused: ScriptSystem::FrameUpdate is
//  gated on an unpaused clock, so no script ticks and OnStart never runs. A
//  `SpriteComponent*` member would therefore be null for an entire editing
//  session no matter what you dropped on it, and a pointer has no form in a
//  scene file, so the drop would have nowhere to be saved.
//
//  Nor could it survive. The behaviour object is destroyed and rebuilt on
//  Stop, on every undo step, on every scene change, on a script rebuild and
//  on a prefab re-save - and a stored EntityId is no rescue, because
//  Scene::Unload clears the slot table and generations restart at 1, so a
//  stale id can be VALID and resolve to a different entity. There is a test
//  named for it: "names survive a snapshot and entity ids do not".
//
//  So the reference travels as the one thing that does survive all of that -
//  the target entity's NAME, in the base class - and resolves on every
//  access. That is the same bargain eng::EntityRef already made, and the
//  reason the whole of its machinery is inherited here rather than rebuilt:
//  the scene-file format, the Inspector slot, the drag target, the picker,
//  the rename-following and the red-when-broken display all work already.
//
//  THE COST IS A LOOKUP PER ACCESS, by entity name. That is the same cost the
//  house rule already asks for - "do not keep the pointer, ask with Find each
//  time" - and for a reference across entities it is not optional.
//
//  ==========================================================================
//  WHAT `->` HANDS BACK, and the one place it is not the component
//
//  Transform is the exception, and it is what makes the two spellings agree
//  rather than an inconsistency. Every script already reaches its own
//  transform as `transform->WorldPosition()` - see the handle in
//  scene/ScriptComponent.h - so that is the line everybody writes. If
//  `TransformRef anchor;` handed back the COMPONENT, the
//  same question one entity away would read
//  `anchor->Transform().WorldPosition()` - "transform's Transform" - and the
//  two would not look like the same thing. Unity has no such seam:
//  `other.transform.position` is spelled exactly like `transform.position`.
//
//  So a ComponentRef<TransformComponent> resolves one step further, to the
//  Transform2D the component holds. The component itself is still there under
//  GetComponent() for the rare caller that wants it.
//
//  ==========================================================================
//  WHAT IT IS NOT FOR
//
//  A COMPONENT ON THE SAME ENTITY. Ask for that where you use it -
//  `Find<eng::Rigidbody2DComponent>()` - or cache the raw pointer in a
//  private member, which is safe there for the reason it is not safe here:
//  the cache and the behaviour object die together. assets/scripts has four
//  of those and they are all correct.
//
//  ANOTHER SCRIPT. A script class sits in an anonymous namespace in its own
//  .cpp, so no other file can name the type - which is why FindScriptOn
//  cannot live in EntityRef.h either, and why the worked example in
//  assets/scripts/Bullet.cpp reaches a ScoreKeeper by message instead. Use
//  eng::EntityRef with a `// [ScoreKeeper]` marker to get the slot, and
//  FindScriptNamedIn or a eng::Message to do the talking.
// ============================================================================

#include <engine/scene/Component.h>
#include <engine/scene/EntityRef.h>

#include <type_traits>

namespace eng {

// ---------------------------------------------------------------------------
//  What a reference to this component resolves TO. The component itself for
//  everything except TransformComponent - see the note above for why that one
//  goes one step further, and why that is the consistent answer rather than
//  the surprising one.
// ---------------------------------------------------------------------------
template <class T>
struct ComponentRefTarget {
    using type = T;
};

template <>
struct ComponentRefTarget<TransformComponent> {
    using type = Transform2D;
};

template <class T>
class ComponentRef : public EntityRef {
public:
    // Every engine component carries a kTypeName, and that name is the whole
    // of what the Inspector needs to filter its picker and to decide whether
    // a dropped entity is the right one. A type without one is not a
    // component, and the message says what to write instead - this is the
    // mistake the owner of a Transform reference will make first.
    static_assert(std::is_base_of_v<::eng::Component, T>,
                  "ComponentRef<T> needs a COMPONENT type. eng::Transform2D is not "
                  "one - it is the value a TransformComponent holds, so write "
                  "ComponentRef<eng::TransformComponent>, or TransformRef.");
    static_assert(requires { T::kTypeName; },
                  "ComponentRef<T> needs a CONCRETE component - one with a "
                  "kTypeName. An abstract base has none, and Entity::Find looks a "
                  "component up BY that name with no derived-to-base step, so a "
                  "reference to eng::ColliderComponent could never resolve. Name "
                  "the collider you mean: ComponentRef<eng::BoxColliderComponent>. "
                  "eng::FindCollider(Entity&) is the hand-written answer to the "
                  "other question.");

    using ComponentType = T;

    // NOT `Target`, deliberately: EntityRef::Target() is an inherited member
    // function, and a type alias of the same name would hide it.
    using Pointee = typename ComponentRefTarget<T>::type;

    // What the Inspector constrains the slot to. Taken from the type rather
    // than from a `// [SpriteComponent]` comment, which is the point of the
    // whole class: the constraint and the thing you call cannot drift apart,
    // because they are the same declaration.
    static constexpr const char* kRequiredType = T::kTypeName;

    // Null when nothing is referenced, when the entity has gone, when it was
    // renamed to something else, or when it never had the component - all of
    // which the caller handles the same way, which is why they are not told
    // apart. Use IsSet() to ask whether a name was ever filled in.
    Pointee* Get() const {
        T* component = EntityRef::Find<T>();
        if (component == nullptr) {
            return nullptr;
        }
        if constexpr (std::is_same_v<Pointee, T>) {
            return component;
        } else {
            // Only ever instantiated for TransformComponent.
            return &component->Transform();
        }
    }

    // The component itself, even where Get() resolves past it. Only
    // TransformComponent has anything past it, so for every other type these
    // two are the same call.
    T* GetComponent() const { return EntityRef::Find<T>(); }

    Pointee* operator->() const { return Get(); }

    // `if (anchor)` - true only when it resolves right now, so it answers the
    // question callers actually have rather than "was something dropped on
    // it". Hides EntityRef's, which answers about the entity alone.
    explicit operator bool() const { return Get() != nullptr; }
};

} // namespace eng
