#pragma once

// ============================================================================
//  ScriptRef.h - a script pointing at ANOTHER SCRIPT on another entity.
//
//  The sibling of ComponentRef. That one reaches a component the engine
//  defines; this one reaches a class the game defines:
//
//      #include "Bullet.h"
//
//      class Chaser final : public eng::ScriptBehaviour {
//      public:
//          ScriptRef<Bullet> target;   // drag an entity carrying a Bullet here
//
//          void OnFixedUpdate() {
//              if (target) {
//                  target->Explode();      // Bullet's own method
//              }
//          }
//      };
//
//  You drag an entity from the Hierarchy onto the slot in the Inspector, and
//  the picker offers only the entities that actually carry a Bullet. The slot
//  is the same one a component reference gets, because it IS the same slot:
//  everything here is inherited from eng::EntityRef.
//
//  ==========================================================================
//  WHY IT STORES A NAME, which is the first thing anybody asks
//
//  For the four reasons ComponentRef.h sets out at length, every one of which
//  applies here unchanged: the Inspector matters in EDIT mode, where no script
//  ticks and OnStart never runs, so a pointer member could never be filled in;
//  a pointer has no form in a scene file, so a drop would have nowhere to be
//  saved; the behaviour object is destroyed and rebuilt on Stop, on every undo
//  step, on every scene change, on a script rebuild and on a prefab re-save;
//  and a stored EntityId is no rescue, because Scene::Unload clears the slot
//  table and generations restart at 1, so a stale id can be VALID and resolve
//  to a different entity.
//
//  So the reference travels as the one thing that survives all of that - the
//  target entity's NAME, in the base class - and resolves on every access.
//  The scene-file format, the Inspector slot, the drop target, the picker, the
//  rename-following and the red-when-broken display are all inherited rather
//  than rebuilt.
//
//  ==========================================================================
//  THE CLASS HAS TO BE VISIBLE, and that is the one thing this costs
//
//  Scripts share the global namespace, so nothing HIDES the name - they used
//  to each sit in an anonymous `namespace { }` and no longer do. What
//  Chaser.cpp lacks is a DECLARATION of a class defined in Bullet.cpp, and C++
//  needs a complete type to call anything on one.
//
//  So a script you intend to point at lives in a header beside the others:
//
//      assets/scripts/Bullet.h     the class, and ENGINE_REGISTER_SCRIPT
//      assets/scripts/Chaser.cpp   #include "Bullet.h"
//
//  A quoted include resolves against the directory of the file that writes it,
//  so `#include "Bullet.h"` beside the script just works with no build setting
//  to change. From a sub-folder, count the `../` yourself.
//
//  AND THE METHODS YOU CALL HAVE TO BE DEFINED IN THE HEADER, in the class or
//  `inline`. Each script compiles into its own library and nothing exports a
//  script's symbols, so a body left in a .cpp beside the header will not link.
//
//  ==========================================================================
//  WHY THE de-NAMESPACING WAS A PRECONDITION AND NOT A CONVENIENCE
//
//  Resolving the reference ends in a dynamic_cast, and the object was BUILT IN
//  A DIFFERENT LIBRARY: one DLL per script, each with its own copy of the
//  class's RTTI from the shared header. That works because MSVC matches
//  type_info by DECORATED NAME rather than by address - measured, not assumed.
//
//  An anonymous namespace salts that decorated name per translation unit
//  (`.?AVBullet@?A0x1b909511@@` in one library, a different salt in the
//  other), so the cast returned null however identical the layout. The old
//  one-anonymous-namespace-per-script arrangement could not have supported
//  this feature at all.
//
//  The other way to break it is building one side with /GR-, which hangs
//  inside dynamic_cast rather than returning null. The script build hard-codes
//  its switches and never passes it; there is no project setting that can.
//
//  ==========================================================================
//  WHAT IT IS NOT FOR
//
//  ONE-TO-MANY, or anything open-ended. A reference is one slot somebody drags
//  once. "Every pickup in the scene" is a collider layer and a query;
//  "whichever one is nearest" changes every step and cannot be dragged at all.
//  assets/scripts/GameRules.cpp and Autopilot.cpp are both right to be
//  data-driven instead, and converting either would be a step backwards.
//
//  A TARGET THAT DOES NOT EXIST YET. A bullet spawned from a prefab at runtime
//  cannot have had a scorekeeper dragged onto it. assets/scripts/Bullet.cpp
//  talks to ScoreKeeper by eng::Message for exactly that reason, and because
//  the payload IS the whole content - there is no method to call.
//
//  ACROSS A SCENE. A reference resolves against the scene that is running, so
//  one pointed into a level that is not loaded is simply null.
//
//  A SCRIPT ON THE SAME ENTITY. Ask for it where you use it, with
//  eng::FindScriptOn<T>(*Owner()).
// ============================================================================

#include <engine/scene/EntityRef.h>

#include <type_traits>

namespace eng {

class Entity;
class ScriptBehaviour;

// DECLARED, NOT INCLUDED, and it has to be this way round.
//
// FindScriptOn is defined in scene/ScriptComponent.h, which cannot be included
// from here: it includes ScriptFields.h, which includes THIS file so that it
// can specialise fields::Traits for the type below. A declaration is all the
// call in Get() needs - it is a dependent call, resolved when ScriptRef<T> is
// instantiated, and by then every script has the whole of Engine.h.
template <class T>
T* FindScriptOn(Entity& entity);

template <class T>
class ScriptRef : public EntityRef {
public:
    // Checked at instantiation, when ScriptBehaviour is complete - this header
    // only has its name. The message names the two mistakes worth catching:
    // a component, which has its own reference type, and a plain struct that
    // somebody forgot to derive.
    static_assert(std::is_base_of_v<::eng::ScriptBehaviour, T>,
                  "ScriptRef<T> needs a SCRIPT type - a class deriving from "
                  "eng::ScriptBehaviour, with ENGINE_REGISTER_SCRIPT(T) somewhere. "
                  "For a component the engine defines, the reference is "
                  "eng::ComponentRef<T> instead - or one of its Unity-named "
                  "aliases, SpriteRenderer, Rigidbody2D, TransformRef.");

    using ScriptType = T;

    // NOT a kRequiredType, and that is the one honest regression against
    // ComponentRef. A component carries T::kTypeName, so that class can take
    // the Inspector's constraint FROM the type and the two can never drift. A
    // script class carries no such thing - its name exists only as the text
    // inside ENGINE_REGISTER_SCRIPT - so the constraint is lifted out of the
    // template argument by the variable extractor instead. It still cannot
    // drift from the type, because it is read off the same declaration; see
    // tools/ScriptVars.cpp. A HAND-WRITTEN OnExpose can get it wrong, and
    // ScriptFields::Ref's unrecognised-name warning is the only net there.

    // Null when nothing is referenced, when the entity has gone, when it was
    // renamed to something else, and when the entity carries no T - all of
    // which a caller handles the same way, which is why they are not told
    // apart. IsSet() answers whether a name was ever filled in.
    T* Get() const {
        Entity* entity = EntityRef::Target();
        if (entity == nullptr) {
            return nullptr;
        }
        return FindScriptOn<T>(*entity);
    }

    T* operator->() const { return Get(); }

    // `if (target)` - true only when it resolves RIGHT NOW, so it answers the
    // question callers actually have rather than "was something dropped on
    // it". Hides EntityRef's, which answers about the entity alone.
    explicit operator bool() const { return Get() != nullptr; }
};

} // namespace eng
