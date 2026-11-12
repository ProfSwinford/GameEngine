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
//  YOU DO NOT WRITE THIS TYPE, AND THAT IS THE POINT
//
//  A script's BARE NAME is an alias for a reference to it, so the declaration
//  somebody actually writes is
//
//      Door door;              // then door->Open();
//
//  with no header, no include and no wrapper. The build arranges it: every
//  generated unit DECLARES AND ALIASES EVERY SCRIPT IN THE PROJECT before it
//  defines any of them, and then wraps each one in a namespace of its own -
//
//      namespace s_Door { class Door; }
//      using Door = ::eng::ScriptRef<s_Door::Door>;
//      namespace s_Door { ... Door.cpp ... }
//
//  so inside Door.cpp the name is the CLASS and everywhere else it is a
//  REFERENCE. See GenerateScriptUnit in engine/tools/ScriptVars.h.
//
//  A REFERENCE MAY POINT EITHER WAY, OR BOTH, and the declaration above is
//  what buys that: this class keeps a NAME, so its size and layout are the
//  same whatever T is, and the only member that needs T COMPLETE is Get() -
//  instantiated where the reference is used, which is why the static_assert
//  lives in there rather than in the class body. So two scripts can point at
//  each other with no header between them and no order to get right;
//  assets/scripts/Script References/ is that pair.
//
//  AND THREE PLACES THE BARE NAME IS NOT THE SPELLING. Each is a C++ rule
//  rather than a limit of this build, each has a spelling that works, and the
//  script build says which one before the compiler does.
//
//    A REFERENCE TO ITS OWN KIND - a door that opens the next door along.
//    Inside `class Door` the name Door is the class being defined; that is
//    the injected-class-name and no arrangement of namespaces changes it. So
//    write ScriptRef<Door>, which means the same thing everywhere.
//
//    DERIVING FROM ANOTHER SCRIPT. `class Sliding : public Door` asks to
//    inherit from a handle. Write `public Door::ScriptType` - the alias below
//    declares that name for exactly this - and take `final` off the script
//    being derived from.
//
//    TWO SCRIPTS THAT EACH CALL THE OTHER. `other->Method()` is ordinary
//    member lookup and needs the other class FINISHED, so the caller is
//    compiled after it, and in a loop each would have to come after the next.
//    Two C++ classes whose inline methods call each other cannot be written
//    in one file either. Let one side do the calling and the other hold the
//    slot, send an eng::Message, or move the shared part into a .cpp of its
//    own. HOLDING a reference is never restricted this way - declaring it,
//    `if (other)`, saving it, dragging it and following a rename all work in
//    either direction.
//
//  WHY THE NAMESPACE IS THE SAME IN EVERY UNIT, including the script's own:
//  see the measured note below. ::Door and s_Door::Door are different
//  decorated names, and the cast matches on the name.
//
//  WRITING ScriptRef<Door> STILL WORKS. The bare name is already a reference,
//  so that asks for a reference to a reference - which the specialization at
//  the bottom of this file collapses. Nothing older breaks.
//
//  A HEADER STILL WORKS TOO. Put the class in Bullet.h, include it from
//  Bullet.cpp, keep the registrar in the .cpp, and the build folds the header
//  into that script's namespace so the class lands in the same place. The
//  registrar stays in ONE .cpp either way: in the header it registers once per
//  includer, which the registry recognises rather than complaining about, but
//  then which library owns the class depends on load order.
//
//  THE WORKED EXAMPLE is assets/scripts/Script References/ - Door.cpp and
//  DoorSwitch.cpp, two files and no header - and
//  assets/scenes/script_references.oscene is the pair wired up and running.
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
    // THE SCRIPT CLASS A BARE NAME STANDS FOR, and the spelling for the one
    // thing the bare name cannot do: `class Sliding final : public
    // Door::ScriptType` is how one script derives from another, because a
    // base class has to be the CLASS and `Door` on its own is a reference to
    // it. Also what a static member is reached through -
    // Door::ScriptType::SomeHelper(). Not a name anybody needs otherwise.
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
        // ==================================================================
        //  CHECKED HERE RATHER THAN IN THE CLASS BODY, and the difference
        //  matters more than it looks.
        //
        //  A static_assert in the class body is instantiated when the MEMBER
        //  IS DECLARED, which forces T to be complete at that point. That
        //  made two scripts referring to each other impossible to compile in
        //  any order: whichever came second was still incomplete when the
        //  first declared its reference, and the failure was not even the
        //  helpful message below - it was
        //
        //      error C2139: 'Beta': an undefined class is not allowed as an
        //      argument to compiler intrinsic type trait '__is_base_of'
        //
        //  pointed at this header rather than at either script.
        //
        //  Down here it is instantiated when the reference is USED, by which
        //  time T has to be complete anyway - FindScriptOn dynamic_casts to
        //  it. So nothing that was diagnosed stops being diagnosed: every
        //  real use goes through Get(), including operator-> and the bool
        //  conversion. A reference that is declared and never touched is the
        //  one case that now says nothing, which is the right trade for
        //  making a mutual reference expressible at all.
        // ==================================================================
        static_assert(std::is_base_of_v<::eng::ScriptBehaviour, T>,
                      "ScriptRef<T> needs a SCRIPT type - a class deriving from "
                      "eng::ScriptBehaviour, with ENGINE_REGISTER_SCRIPT(T) somewhere. "
                      "For a component the engine defines, the reference is "
                      "eng::ComponentRef<T> instead - or one of its Unity-named "
                      "aliases, SpriteRenderer, Rigidbody2D, TransformRef.");

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

// ===========================================================================
//  ScriptRef<ScriptRef<T>> IS ScriptRef<T>, and this is what keeps the OLD
//  SPELLING compiling.
//
//  A script's bare name is now an alias for a reference to it - `Door door;`
//  is the whole point - so somebody who writes the explicit form,
//  `ScriptRef<Door> door;`, is asking for ScriptRef<ScriptRef<the class>>.
//  Without this specialization that is a hard error from the static_assert in
//  Get(), and a line that was correct yesterday, is in every older example,
//  and is still the right thing to write in a hand-written header stops
//  compiling for a reason nobody could act on.
//
//  DERIVED RATHER THAN ALIASED, because a partial specialization cannot be an
//  alias template. Everything useful comes from the base unchanged: Get()
//  returns the SCRIPT rather than a reference to one, operator-> reaches its
//  methods, and fields::Traits already matches any ScriptRef at all, so the
//  Inspector sees no difference.
// ===========================================================================
template <class T>
class ScriptRef<ScriptRef<T>> : public ScriptRef<T> {};

} // namespace eng
