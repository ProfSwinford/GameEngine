#pragma once

// ============================================================================
//  ScriptComponent.h - writing your own behaviour for an entity.
//
//  This is the engine's version of a Unity MonoBehaviour. You write a class,
//  give it whichever lifecycle functions you actually want, and it runs:
//
//      class Bouncer : public eng::ScriptBehaviour {
//      public:
//          void OnFixedUpdate() {
//              transform->Translate({0, Time::DeltaTime() * 50});
//          }
//      };
//      ENGINE_REGISTER_SCRIPT(Bouncer)      // without this it can never be found
//
//  ==========================================================================
//  WRITE ONLY THE HOOKS YOU WANT. NO `virtual`, NO `override`, NO STUBS.
//
//  A script that only cares about collisions writes only OnCollisionEnter. It
//  does not inherit six empty functions and it does not override anything.
//
//  This works the way a C# engine's reflection works - "does this class have
//  an Update method?" - except the question is asked by the COMPILER, once,
//  while your script is being built. There is no reflection at run time, no
//  lookup by string, and nothing to allocate.
//
//  How that is done is the most advanced code in the engine, so it is kept out
//  of the way in scene/ScriptHooks.h. You never need to read it to write a
//  script, and nothing in this file depends on understanding it.
//
//  THE PAYOFF IS SPEED AS WELL AS CONVENIENCE. Because the engine knows at
//  build time which hooks a script actually has, a script with no update is
//  never put in the update list at all. It costs exactly nothing per frame
//  rather than a call into an empty function sixty times a second.
//
//  ==========================================================================
//  THE ONE THING THIS COSTS, AND WHAT IS DONE ABOUT IT
//
//  `override` used to catch a misspelled hook at compile time: writing
//  OnUpdat instead of OnUpdate was an error. Now it is simply a function
//  nobody calls, and your script silently does nothing - which is the classic
//  bug in every engine that works this way.
//
//  Three things push back on that, and all of them are in
//  ENGINE_REGISTER_SCRIPT:
//
//    1. A script with NO hooks at all is a compile error. That is the one that
//       catches a lone misspelled hook in a script that has only one.
//    2. A hook with the RIGHT NAME and the WRONG SIGNATURE is a compile error
//       that says what the signature should be. That is the common mistake.
//    3. The names from other engines - Update, Start, Awake, FixedUpdate - are
//       compile errors that name the hook you meant instead.
//
//  And the Console lists every script it loaded together with the hooks it
//  found, so "my script does nothing" is a question you can answer by looking.
//
//  ==========================================================================
//  YOUR HOOKS MUST BE PUBLIC.
//
//  The engine calls them from outside your class, so a private OnUpdate is
//  invisible to it - exactly as if you had not written one. This is the one
//  place a C# engine using reflection is genuinely more forgiving, because
//  reflection ignores access and a compiler cannot.
//
//  It is not silent, though: a script whose hooks are all private has no
//  visible hooks at all, and rule 1 above turns that into a compile error
//  that says to check both the spelling and the `public:`.
//
//  ==========================================================================
//  VARIABLES IN THE INSPECTOR, which take no code at all
//
//  A PUBLIC variable of a supported type is on screen, editable while the game
//  runs, and saved on each entity separately:
//
//      float speed = 200.0f;      // how fast it goes
//      float blend = 0.5f;        // [0..1] how much to mix
//      eng::EntityRef target;     // [Rigidbody2DComponent] what to push
//
//  THE HOOKS ABOVE ARE FOUND BY THE COMPILER; THESE ARE NOT, and cannot be. A
//  `requires` expression can ask whether a NAMED member exists, and no C++
//  construct can ask what members exist at all - so the editor reads the
//  script's SOURCE when it builds it and generates the registration. See
//  tools/ScriptVars.h for how, and what it deliberately does not understand.
//
//  A script can still write the list itself with OnExpose, and the
//  hand-written one wins. ScriptFields.h has that and the reason the scene
//  file stores only the values somebody changed; EntityRef.h has what a
//  reference to another entity is, and FindScriptOn further down this file is
//  how to reach another script through one.
//
//  ==========================================================================
//  ONE HONEST DIFFERENCE FROM UNITY
//  A script here is COMPILED C++, not an interpreted file. There is no
//  scripting language and no virtual machine. The editor compiles your scripts
//  for you when it regains focus, so you never rebuild the editor - but there
//  is a compiler involved, and it will tell you when your code is wrong.
//
//  ==========================================================================
//  THE THREE PIECES
//    ScriptBehaviour   what you inherit from. It gives you Transform(),
//                      Owner() and GetScene() - and declares NO hooks, which
//                      is what lets the compiler tell whether YOU wrote one.
//    ScriptRegistry    a table of name -> how to make one + which hooks it
//                      has, filled in by the ENGINE_REGISTER_SCRIPT macro.
//    ScriptComponent   the engine component. It stores a NAME, and an instance
//                      if that name exists in this build.
//
//  WHY THE COMPONENT STORES A NAME RATHER THAN A TYPE
//  Because the editor has to be able to attach a script that has not been
//  compiled yet. Drop "PlayerController" onto an entity in a build where
//  PlayerController.cpp does not exist and the component attaches, saves, and
//  shows as UNRESOLVED in red in the Inspector. Rebuild, reload, and the same
//  scene file produces a working behaviour with nothing reattached.
//
//  UNRESOLVED IS ALWAYS REPORTED. A script that does nothing because its name
//  is misspelled, and says nothing about it, is an afternoon lost.
// ============================================================================

#include <engine/scene/Component.h>
// Scene.h, because the templates on ScriptBehaviour below walk the scene and
// the hierarchy, so Scene and Entity both have to be complete here rather
// than merely named. Nothing Scene.h includes includes this file back.
#include <engine/scene/Scene.h>
#include <engine/scene/ScriptFields.h>
#include <engine/scene/ScriptHooks.h>
#include <engine/scene/SystemOrder.h>

#include <cstddef>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
namespace eng {

class ScriptComponent;

// ===========================================================================
//  THE BARE NAMES - transform, gameObject, scene, name, tag.
//
//  Unity's MonoBehaviour reaches these as properties, written as though they
//  were local variables, and that is what a script here writes too:
//
//      transform->Translate({0.0f, speed * Time::DeltaTime()});
//      if (gameObject->HasTag("Player")) { ... }
//      ENGINE_LOG_INFO(Channels::kGame, "{} was hit", name);
//
//  C++ has no properties, so each one is a DATA MEMBER of a tiny handle type
//  that resolves through operator-> on every use. Two other designs were
//  possible and both are worse:
//
//    A MACRO - #define transform Transform() - rewrites the token everywhere,
//    including in the eight scripts in assets/scripts that USED `transform` as
//    a local until this change removed them, and in any variable a game
//    happens to name that.
//
//    A CACHED POINTER - Transform2D* transform, filled in at bind - is what
//    Unity does and costs nothing, and it cannot be done here. A behaviour
//    built by ScriptRegistry::Create has no component yet, which is how the
//    tests build one, so at construction there is nothing to cache. And a
//    cached pointer would turn the null answers this class promises into a
//    dangling read, which is the one failure worse than no answer.
//
//  So: a handle holding nothing but the behaviour it belongs to, asking
//  afresh every time. One extra pointer load, and it cannot go stale.
//
//  =========================================================================
//  WHAT A HANDLE WILL AND WILL NOT DO
//
//      transform->WorldPosition()      the thing itself, asked for now
//      Transform2D* p = transform;     converts, so it passes anywhere the
//                                      pointer does
//      if (transform) { ... }          false once there is no entity
//
//  A HANDLE IS NOT ASSIGNABLE FROM ANOTHER HANDLE. `transform = somethingElse`
//  is a compile error rather than a quiet rebinding of what `transform` means
//  for the rest of this object's life. `tag` is the single exception and it
//  takes a STRING, because setting a tag is an ordinary thing to do.
//
//  transform->X() ON A DESTROYED ENTITY IS A NULL DEREFERENCE, exactly as
//  Transform()->X() already was. A handle does not make that safe and does not
//  pretend to - it answers null, and `if (transform)` is how a script that
//  genuinely cannot know asks. In practice a script stops being ticked before
//  its entity goes, so the checks that eight of the fourteen scripts in
//  assets/scripts do write are belt-and-braces rather than load-bearing.
//
//  =========================================================================
//  THE ONE THING THIS COSTS, WRITTEN DOWN BEFORE SOMEBODY MEETS IT
//
//  A member found by unqualified lookup hides namespace-scope names of the
//  same spelling AND suppresses argument-dependent lookup. So inside a script
//  `transform` is this handle, and an unqualified call to std::transform stops
//  finding it:
//
//      transform(v.begin(), v.end(), out.begin(), fn);   // was std::transform
//
//  compiles today, and afterwards says "term does not evaluate to a function
//  taking 4 arguments" - naming neither std::transform nor this member. WRITE
//  std::transform, which is unaffected and is the better spelling anyway.
//  None of the other four names has a standard-library counterpart, so that
//  is the whole of it.
//
//  Case is what keeps the rest apart: every one of these is lowerCamelCase and
//  every type in the engine is UpperCamelCase, so `transform` and Transform2D,
//  `scene` and Scene, `gameObject` and GameObject all coexist. All 323 names
//  that <engine/Engine.h> makes visible unqualified were checked against these
//  five, and std::transform is the only collision in either direction.
// ===========================================================================

// ---------------------------------------------------------------------------
//  What every handle holds: one pointer, and no state about the world.
//
//  Copyable, so `auto t = transform;` works and means the same thing. NOT
//  assignable, for the reason above.
// ---------------------------------------------------------------------------
class ScriptHandle {
public:
    explicit ScriptHandle(const ScriptBehaviour* self) : m_self(self) {}

    ScriptHandle(const ScriptHandle&)            = default;
    ScriptHandle& operator=(const ScriptHandle&) = delete;

protected:
    // Not public: a script has no business reaching back through a handle to
    // the behaviour it already is.
    const ScriptBehaviour* Script() const { return m_self; }

private:
    const ScriptBehaviour* m_self;
};

// `transform` - Unity's Component.transform.
class TransformHandle : public ScriptHandle {
public:
    using ScriptHandle::ScriptHandle;

    Transform2D* operator->() const;
    operator Transform2D*() const;
};

// `gameObject` - Unity's Component.gameObject. The engine's word for the thing
// is an Entity; `GameObject` is already a typedef for Entity* in Engine.h, and
// this is that same translation one level down.
class EntityHandle : public ScriptHandle {
public:
    using ScriptHandle::ScriptHandle;

    Entity* operator->() const;
    operator Entity*() const;
};

// `scene` - NOT a Unity name. Unity's nearest is gameObject.scene, which is a
// different and smaller thing. It is here because GetScene() is the third most
// written call in the engine's own scripts, almost always immediately followed
// by a null check and one use.
class SceneHandle : public ScriptHandle {
public:
    using ScriptHandle::ScriptHandle;

    Scene* operator->() const;
    operator Scene*() const;
};

// ---------------------------------------------------------------------------
//  `name` and `tag` - Unity's Object.name and Component.tag.
//
//  The two differ in one way that matters, and it is not a difference Unity
//  has:
//
//    A TAG IS A LABEL, and setting one is free - so `tag = "Enemy"` works.
//
//    A NAME IS AN IDENTITY here. Parents and entity references are recorded BY
//    NAME, the scene keeps a name-to-entity table, and two entities called
//    "Bullet" is a bug - which is why Scene::CreateEntityUniquelyNamed exists
//    and why Scene::RenameEntity can REFUSE. So `name` reads and does not
//    assign: Rename() is the verb, and it hands back whether it worked.
//
//  Both read "" once the entity has gone, which is the answer Name() already
//  gives.
//
//  REACHED WITH ->, like every other handle: `name->size()`, `name->c_str()`,
//  `tag->empty()`. Both convert to const std::string& as well, and both have a
//  std::formatter at the bottom of this file so that ENGINE_LOG_*("{}", name)
//  works.
//
//  THAT CONVERSION DOES NOT REACH A std::string_view PARAMETER, because an
//  implicit conversion sequence may contain at most one user-defined step and
//  handle -> const std::string& -> string_view is two. Every string-taking
//  verb on this class takes string_view - HasTag, Rename, SetTag, FindEntity,
//  FindEntityWithTag - so `HasTag(tag)` does not compile and `HasTag(Tag())`
//  is the spelling. The surface is deliberately left at one conversion: a
//  second would also make `tag = name` legal, which is not worth spelling.
// ---------------------------------------------------------------------------
class NameHandle : public ScriptHandle {
public:
    using ScriptHandle::ScriptHandle;

    const std::string* operator->() const;
    operator const std::string&() const;

    // DELIBERATELY REFUSED rather than merely absent, so that `name = "x"`
    // produces an error pointing at this line instead of "no operator matched
    // these operands". A name is an identity - see above - and Rename() is the
    // one that can report a collision.
    void operator=(std::string_view) = delete;
};

class TagHandle : public ScriptHandle {
public:
    using ScriptHandle::ScriptHandle;

    const std::string* operator->() const;
    operator const std::string&() const;

    // Unity's `tag = "Enemy"`. Does nothing once the entity has gone, like
    // every other verb on this class.
    //
    // CONST, because it changes nothing of the handle's own: the back-pointer
    // is a const ScriptBehaviour* and SetTag() is itself const, as every verb
    // on that class is. Non-const would buy no protection and would make
    // `tag = "x"` fail in exactly the places SetTag("x") succeeds.
    const TagHandle& operator=(std::string_view newTag) const;
};

// ---------------------------------------------------------------------------
//  The class you inherit from.
//
//  Note what is NOT here: OnStart, OnUpdate, OnFixedUpdate, OnDestroy,
//  OnCollisionEnter. If
//  this class declared them, every script would appear to have them and the
//  compiler could never tell which ones you actually wrote. Their absence is
//  the whole mechanism.
//
//  What it does give you is the handful of accessors a script needs, all valid
//  from OnStart onwards.
// ---------------------------------------------------------------------------
class ScriptBehaviour {
public:
    // Virtual so a script can be deleted through this base pointer. It is the
    // only virtual function left, and the only one that needs to be.
    virtual ~ScriptBehaviour() = default;

    // ======================================================================
    //  NOT COPYABLE, AND THAT IS NEW.
    //
    //  Each handle below holds the address of THIS object. A copy would carry
    //  handles still pointing at the original, so `transform` on the copy
    //  would move the original's entity - silently, and only in code that
    //  copied a script, which nothing does. Scripts are made once through
    //  std::unique_ptr and never copied, so this shuts a door rather than one
    //  that was in use.
    //
    //  The default constructor has to be written out because declaring any
    //  other constructor - even a deleted one - suppresses it, and
    //  ScriptRegistry::Create needs it.
    // ======================================================================
    ScriptBehaviour()                                  = default;
    ScriptBehaviour(const ScriptBehaviour&)            = delete;
    ScriptBehaviour& operator=(const ScriptBehaviour&) = delete;
    ScriptBehaviour(ScriptBehaviour&&)                 = delete;
    ScriptBehaviour& operator=(ScriptBehaviour&&)      = delete;

    Entity*      Owner() const;
    EntityId     OwnerId() const;
    Scene*       GetScene() const;
    Transform2D* Transform() const;

    // ======================================================================
    //  THE SAME THINGS, SPELLED THE WAY UNITY SPELLS THEM.
    //
    //  transform, gameObject and scene are the three accessors just above;
    //  name and tag are the two just below. They add no capability at all -
    //  they are the spelling that lets a line read as the thing it does:
    //
    //      transform->Translate(step)   rather than  Transform()->Translate(step)
    //
    //  The long note above ScriptHandle has how they work, what they refuse to
    //  do, and the one standard-library name `transform` hides.
    // ======================================================================
    TransformHandle transform{this};
    EntityHandle    gameObject{this};
    SceneHandle     scene{this};
    NameHandle      name{this};
    TagHandle       tag{this};

    // ======================================================================
    //  THE VERBS. Everything below is a convenience, and every one of them
    //  replaces a specific thing scripts in this project were actually
    //  writing out by hand.
    //
    //  NONE OF THEM REMEMBER ANYTHING ABOUT THE WORLD. Each one asks the scene
    //  afresh, so there is no state here to go stale across a frame, across a
    //  script reload, or across the entity being destroyed. Every one of them
    //  is safe to call on a script whose entity has already gone - they answer
    //  null, or do nothing - which is why there is no IsAlive() to check
    //  first.
    //
    //  The five handles above hold one pointer each, to this object, and that
    //  is the only thing held anywhere in this class besides m_component. They
    //  hold no scene, no entity and no transform, which is the property this
    //  paragraph is really about: ASKING AFRESH is what makes the answers
    //  trustworthy, and a handle asks on every use.
    // ======================================================================

    // This entity's name, or "" once it has gone. Scripts used to write
    //     Owner() != nullptr ? Owner()->Name() : "<none>"
    // in every log line; this is that, and the empty string is the honest
    // answer rather than a placeholder somebody has to recognise. They write
    // `name` now - see the std::formatter at the bottom of this file.
    const std::string& Name() const;

    // ======================================================================
    //  Unity's `name = "..."`, which cannot be an assignment here.
    //
    //  A name is an IDENTITY in this engine, not a label: parents and entity
    //  references are recorded by name and the scene keeps a name-to-entity
    //  table, so renaming goes through Scene::RenameEntity - which refuses
    //  when something else already has that name, and which retargets every
    //  reference when it does not.
    //
    //  FALSE MEANS NOTHING CHANGED, and the reason is in the Console. A
    //  version of this that quietly made the name unique would hand back true
    //  for an entity that is now called something else than the script asked
    //  for, which is the worse half of both options.
    // ======================================================================
    bool Rename(std::string_view newName) const;

    // Unity's Component.tag. "" when untagged AND "" once the entity has gone,
    // the same two answers Name() gives - see Entity::Tag for why empty means
    // untagged and why HasTag("") is therefore false.
    const std::string& Tag() const;
    void               SetTag(std::string_view newTag) const;

    bool HasTag(std::string_view wantedTag) const;

    // Unity's gameObject.SetActive / activeInHierarchy, for this entity. See
    // Entity::IsActiveInHierarchy for what being switched off means and why
    // every system in the engine honours it.
    //
    // A SCRIPT THAT SWITCHES ITS OWN ENTITY OFF STOPS RUNNING, from the next
    // step onwards. It finishes the function it is in - nothing is torn down -
    // and then is not ticked again until something else switches it back on.
    // Which means a script cannot switch itself back on, and that is worth
    // knowing before you write it rather than afterwards.
    bool IsActive() const;
    void SetActive(bool active) const;

    // ---- a component on THIS entity --------------------------------------
    //
    // Unity's GetComponent<T>(). Named Find for the reason the house style
    // gives: a lookup that can legitimately come back empty is Find, never
    // Get - and it matches Entity::Find<T> and EntityRef::Find<T>, which
    // already existed and do the same job from the other end.
    //
    //     auto* body = Find<eng::Rigidbody2DComponent>();
    //
    // replaces
    //
    //     eng::Entity* self = Owner();
    //     if (self == nullptr) { return; }
    //     auto* body = self->Find<eng::Rigidbody2DComponent>();
    template <class T>
    T* Find() const {
        Entity* self = Owner();
        return self != nullptr ? self->Find<T>() : nullptr;
    }

    // Unity's GetComponentInChildren / GetComponentInParent. Both include this
    // entity itself, as Unity's do.
    template <class T>
    T* FindInChildren() const {
        Entity* self = Owner();
        return self != nullptr ? self->FindInChildren<T>() : nullptr;
    }
    template <class T>
    T* FindInParents() const {
        Entity* self = Owner();
        return self != nullptr ? self->FindInParents<T>() : nullptr;
    }

    // Unity's AddComponent<T>(). Add always adds another; Require adds one
    // only if there is not one already, which is what the comment "this script
    // needs a rigidbody" usually means.
    template <class T>
    T* Add() const {
        Entity* self = Owner();
        return self != nullptr ? self->Add<T>() : nullptr;
    }
    template <class T>
    T* Require() const {
        Entity* self = Owner();
        if (self == nullptr) {
            return nullptr;
        }
        if (T* existing = self->Find<T>(); existing != nullptr) {
            return existing;
        }
        return self->Add<T>();
    }

    // ---- EVERY T, not just the first --------------------------------------
    //
    // Unity's GetComponents / GetComponentsInChildren / GetComponentsInParent.
    // Several scripts on one entity, or several colliders making one compound
    // shape, are both ordinary - and before these the only spelling was to go
    // through Owner() by hand, which is the four-line dance Find<T> exists to
    // remove.
    //
    // SAME CONTRACT AS Entity::FindAll: `into` is CLEARED first and then
    // filled, the caller owns it so a per-frame walk allocates once, and the
    // return value is how many were written. A script with no entity gets an
    // empty vector and 0 rather than an untouched one, because "cleared first"
    // has to be true however the call turns out.
    template <class T>
    std::size_t FindAll(std::vector<T*>& into) const {
        Entity* self = Owner();
        if (self == nullptr) {
            into.clear();
            return 0;
        }
        return self->FindAll<T>(into);
    }
    template <class T>
    std::size_t FindAllInChildren(std::vector<T*>& into) const {
        Entity* self = Owner();
        if (self == nullptr) {
            into.clear();
            return 0;
        }
        return self->FindAllInChildren<T>(into);
    }
    template <class T>
    std::size_t FindAllInParents(std::vector<T*>& into) const {
        Entity* self = Owner();
        if (self == nullptr) {
            into.clear();
            return 0;
        }
        return self->FindAllInParents<T>(into);
    }

    // ---- another entity --------------------------------------------------
    //
    // THIS IS THE ONE THAT WAS MISSING MOST. Every collision and trigger hook
    // hands over an EntityId, and Instantiate returns one, so almost every
    // interesting script opened with four lines of the same ceremony:
    //
    //     eng::Scene* scene = GetScene();
    //     if (scene == nullptr) { return; }
    //     eng::Entity* partner = scene->Get(other);
    //     if (partner == nullptr) { return; }
    //
    // DO NOT KEEP WHAT COMES BACK. The id is the thing that survives a frame;
    // the pointer is good until the next time entities are destroyed, which is
    // once per step. Ask again rather than remember - the same rule
    // ScriptComponent::Behaviour states, and for the same reason.
    Entity* FindEntity(EntityId id) const;
    Entity* FindEntity(std::string_view entityName) const;

    // Unity's FindWithTag. Null when nothing carries it. Skips anything
    // already queued for destruction, so "find an enemy" does not hand back
    // one that died this tick - see rule 1 in DeferredOps.h.
    Entity* FindEntityWithTag(std::string_view wantedTag) const;

    // Unity's FindObjectOfType<T>: the first entity in the scene carrying a T,
    // and the T itself.
    //
    // THESE DO NOT FIND SCRIPTS. Every script's component is called
    // "ScriptComponent", so FindFirst<Chaser>() cannot work - use
    // FindScriptIn<T> further down this file, which asks each behaviour what it
    // really is.
    // `world` rather than `scene` for the local, here and in ScriptComponent.cpp,
    // because `scene` is now a member of this class and a local of that name
    // would hide it. The scripting reference already calls GetScene() "the
    // world", so the rename costs nothing in readability.
    template <class T>
    Entity* FindFirstWith() const {
        Scene* world = GetScene();
        return world != nullptr ? world->FindFirstWith<T>() : nullptr;
    }
    template <class T>
    T* FindFirstComponent() const {
        Scene* world = GetScene();
        return world != nullptr ? world->FindFirstComponent<T>() : nullptr;
    }

    // ---- making things ---------------------------------------------------
    //
    // Unity's Instantiate. Returns the root of the new instance, or a null id
    // if the prefab could not be loaded - WHICH IS REPORTED HERE rather than
    // handed back as a string for the caller to deal with, because a script
    // has nowhere useful to put an error string and the Console is exactly
    // where this belongs.
    //
    //     Instantiate("prefabs/bullet.oprefab",
    //                 eng::SpawnOptions{}.At(Transform()->WorldPosition())
    //                                    .Facing(Transform()->WorldRotation()));
    //
    // SPAWNING IS SAFE AT ANY TIME, including from inside an update or a
    // collision hook. Creating only ever appends, so nothing a system is
    // walking gets moved out from under it - see the note at the top of
    // DeferredOps.h, which is explicit that destroying is the dangerous half
    // and creating is not.
    EntityId Instantiate(std::string_view prefabPath) const;
    EntityId Instantiate(std::string_view prefabPath, const SpawnOptions& placement) const;

    // The same, through an exposed PrefabRef variable - which is how a spawner
    // should actually be written, because then the prefab is chosen by
    // dragging it onto the script in the Inspector instead of typed into the
    // code. See PrefabRef.h.
    //
    // A reference nobody has set does NOTHING AND SAYS SO ONCE. Not every
    // frame: a spawner firing sixty times a second would otherwise bury the
    // Console in the same line, which is how a real message becomes noise.
    EntityId Instantiate(const PrefabRef& prefab) const;
    EntityId Instantiate(const PrefabRef& prefab, const SpawnOptions& placement) const;

    // An empty entity, named uniquely - Unity's `new GameObject("name")`.
    // Goes through Scene::CreateEntityUniquelyNamed, so calling it fifty times
    // with "Spark" gives fifty distinct names rather than fifty things all
    // claiming to be the same one. See the note on Scene::CreateEntity.
    EntityId CreateEntity(std::string_view baseName) const;
    EntityId CreateEntity(std::string_view baseName, const SpawnOptions& placement) const;

    // ---- unmaking them ---------------------------------------------------
    //
    // Unity's Destroy. Queued, not immediate: it happens at stage 600 once
    // every system has finished. DESTROYING TWICE IS HARMLESS, so there is no
    // need to check first - two bullets hitting one enemy on one tick is
    // ordinary game code and rule 3 exists for it.
    //
    // DestroySelf is the common case and it is spelled separately rather than
    // as a default argument, because `Destroy()` meaning "destroy me" is the
    // kind of thing that reads as a mistake at a call site.
    void DestroySelf() const;
    void Destroy(EntityId id) const;
    void Destroy(Entity& entity) const;

    // Unity's Destroy(obj, t). The delay is GAME time, so it stops while the
    // game is paused, and the thing behaves completely normally until it
    // fires - see QueueDestroyAfter in DeferredOps.h for why that sentence
    // needed a separate list to be true.
    void DestroySelfAfter(float delaySeconds) const;
    void DestroyAfter(EntityId id, float delaySeconds) const;

private:
    friend class ScriptComponent;
    ScriptComponent* m_component = nullptr;
};

// ---------------------------------------------------------------------------
//  The handles' bodies.
//
//  Down here rather than in the classes because every one of them calls a
//  member of ScriptBehaviour, and a function body written inside a class is
//  compiled as though it sat just after that class - which is too early, since
//  ScriptBehaviour is not declared yet at that point.
//
//  Each is one line, and each asks afresh. That is the whole implementation.
// ---------------------------------------------------------------------------

inline Transform2D* TransformHandle::operator->() const { return Script()->Transform(); }
inline TransformHandle::operator Transform2D*() const { return Script()->Transform(); }

inline Entity* EntityHandle::operator->() const { return Script()->Owner(); }
inline EntityHandle::operator Entity*() const { return Script()->Owner(); }

inline Scene* SceneHandle::operator->() const { return Script()->GetScene(); }
inline SceneHandle::operator Scene*() const { return Script()->GetScene(); }

inline const std::string* NameHandle::operator->() const { return &Script()->Name(); }
inline NameHandle::operator const std::string&() const { return Script()->Name(); }

inline const std::string* TagHandle::operator->() const { return &Script()->Tag(); }
inline TagHandle::operator const std::string&() const { return Script()->Tag(); }

inline const TagHandle& TagHandle::operator=(std::string_view newTag) const {
    Script()->SetTag(newTag);
    return *this;
}

// ---------------------------------------------------------------------------
//  `if (tag == "Enemy")`, which does not work without these.
//
//  std::string's own operator== is a TEMPLATE, and template argument deduction
//  does not consider a user-defined conversion - so the handle's conversion to
//  const std::string& cannot reach it. Without these two, `name == "Player"`
//  is a compile error whose message is sixty lines of unrelated candidates
//  (std::monostate, std::pair, std::reverse_iterator and so on), which is a
//  remarkably poor way to learn that a comparison is missing.
//
//  C++20 writes the other three for us: != comes from ==, and both get their
//  reversed forms, so `"Enemy" == tag` works too.
// ---------------------------------------------------------------------------
inline bool operator==(const NameHandle& lhs, std::string_view rhs) {
    return static_cast<const std::string&>(lhs) == rhs;
}
inline bool operator==(const TagHandle& lhs, std::string_view rhs) {
    return static_cast<const std::string&>(lhs) == rhs;
}

// ---------------------------------------------------------------------------
//  AND `name == nullptr` IS REFUSED rather than silently undefined.
//
//  std::string_view's const char* constructor is not explicit, so a null
//  pointer constant reaches it in one user-defined conversion and the view is
//  built by calling char_traits::length on nullptr. C++23 deleted that
//  overload, which is why the engine's own translation units cannot get there
//  - but the editor compiles a project's scripts at /std:c++20, where they
//  can. `Name() == nullptr` has always been an error; these keep the bare
//  spelling an error too, pointing at this line.
//
//  THIS CLOSES THE HANDLE SPELLING ONLY. HasTag(nullptr), FindEntity(nullptr),
//  SetTag(nullptr) and Rename(nullptr) are the same hazard on every
//  string_view parameter in the engine, and are not this file's to answer.
// ---------------------------------------------------------------------------
inline bool operator==(const NameHandle&, std::nullptr_t) = delete;
inline bool operator==(const TagHandle&, std::nullptr_t) = delete;

// ---------------------------------------------------------------------------
//  The table of script names. Same idea as ComponentFactory: something has to
//  turn a name in a file into an object.
// ---------------------------------------------------------------------------
class ScriptRegistry {
public:
    using CreateFn = std::unique_ptr<ScriptBehaviour> (*)();

    // Everything the engine knows about one script type.
    struct Entry {
        CreateFn    create = nullptr;
        ScriptHooks hooks;

        // Which file this class was written in, straight from __FILE__.
        //
        // It earns its place twice. It lets the same script be registered
        // twice harmlessly - which happens whenever a script written in a .h
        // is included by more than one .cpp - and it is what lets the editor
        // notice that a file which used to define `Player` now defines
        // `PlayerController`, and say so instead of leaving every entity
        // pointing at a name that no longer exists.
        std::string sourceFile;
    };

    static void Register(std::string_view scriptName, CreateFn create,
                         const ScriptHooks& hooks, std::string_view sourceFile);

    // ---- the variable list a BUILD STEP worked out ------------------------
    //
    // Installs an exposer for a script that did not write OnExpose, which is
    // how a plain `float radius = 90.0f;` reaches the Inspector with nothing
    // written down. The editor reads the script's source, generates a
    // translation unit that calls this, and compiles that instead of the
    // original. See engine/tools/ScriptVars.h.
    //
    // A HAND-WRITTEN OnExpose IS NEVER OVERWRITTEN, and the function says so
    // by returning false. Somebody who wrote that function stated exactly what
    // they wanted; a guess read out of the source has no business arguing with
    // it. Checking here rather than in the generator means it holds however
    // the call arrives.
    static bool SetExposer(std::string_view scriptName,
                           void (*expose)(ScriptBehaviour*, ScriptFields&));

    static bool         IsRegistered(std::string_view scriptName);
    static const Entry* Find(std::string_view scriptName);

    static std::unique_ptr<ScriptBehaviour> Create(std::string_view scriptName);

    static void ForEachScript(const std::function<void(const char*)>& fn);
    static void ForEachEntry(
        const std::function<void(const char* name, const Entry& entry)>& fn);

    static std::size_t Count();

    // Forgets every registered script.
    //
    // Called by ScriptLibrary just before it unloads the compiled scripts,
    // because every entry in this table is a pointer to a function INSIDE that
    // library. Leaving them behind and then unloading would leave the table
    // full of addresses that no longer exist - and the crash would happen
    // later, somewhere else, the next time somebody attached a script.
    static void Clear();

    // Forgets ONE script. True if there was one to forget.
    //
    // WHY ONE AT A TIME IS NEEDED AT ALL: the editor compiles each script into
    // its own library, so unloading them is no longer a single event. Each has
    // to take exactly its own entries out of this table and leave everybody
    // else's alone - Clear() would take down ten working scripts to reload
    // one. See ScriptLibrary.h.
    static bool Remove(std::string_view scriptName);
};

// ---------------------------------------------------------------------------
//  The component that holds a script.
// ---------------------------------------------------------------------------
class ScriptComponent final : public Component {
public:
    static constexpr const char* kTypeName = "ScriptComponent";

    ~ScriptComponent() override;

    const char* TypeName() const override { return kTypeName; }

    // Scene file fields:
    //   "script": "PlayerController"
    //   "vars":   { "speed": 120.0, "colour": [255, 0, 0, 255] }
    //
    // "vars" holds ONLY the variables somebody changed - see ScriptFields.h
    // for why the defaults are deliberately not written down, and what that
    // buys when a default changes in the code.
    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;

    void OnAttach() override;
    void OnDetach() override;

    const std::string& ScriptName() const { return m_scriptName; }

    // Switches to a different script. Any running behaviour gets its OnDestroy
    // first, so swapping a script in the Inspector tidies up properly rather
    // than dropping the old one on the floor.
    void SetScriptName(std::string_view name);

    // False when the name is not compiled into this build - the "written but
    // not built yet" case. The Inspector shows it in red.
    bool IsResolved() const { return m_behaviour != nullptr; }

    // The running behaviour, or null when the script is not compiled in.
    //
    // HANDED OUT FOR ONE PURPOSE: so FindScriptOn below can ask "is this a
    // Chaser?" about a script on some other entity. Nothing should KEEP what
    // comes back - the object dies with its entity and is replaced wholesale
    // every time scripts are rebuilt, which in the editor is whenever you
    // save a file. Ask again rather than remember, exactly as with an entity.
    ScriptBehaviour* Behaviour() const { return m_behaviour.get(); }

    // Which hooks the bound script turned out to have. The Inspector shows
    // them, because "this script has no update" is the answer to "why is
    // nothing happening" often enough to be worth putting on screen.
    const ScriptHooks& Hooks() const { return m_hooks; }

    // ---- the script's variables ------------------------------------------
    //
    // Empty when the script exposes nothing, and also when it is not compiled
    // into this build - there is no object to point at. The saved values
    // survive both; see SavedValues below.
    //
    // THE POINTERS INSIDE ARE ONLY VALID WHILE THE SCRIPT IS BOUND. Do not
    // hold on to a ScriptField across a frame in which scripts could be
    // rebuilt, which in the editor is any frame at all.
    const ScriptFields& Fields() const { return m_fields; }

    // What goes in the scene file: the variables that differ from the
    // script's own initialisers, as JSON.
    //
    // IT IS KEPT WHEN THE SCRIPT IS NOT. A build without PlayerController
    // compiled in still holds that component's values, writes them back out
    // untouched, and hands them over on the build that has the class. That is
    // why they are stored as JSON rather than as typed values - with no class
    // to ask, there is nothing to say that "speed" is a float.
    const Json& SavedValues() const { return m_savedValues; }

    // Records a variable's CURRENT value as the saved one.
    //
    // The editor changes a variable by writing through the field's own
    // pointer and then calls this, so there is exactly one piece of code that
    // knows how each type is stored. False means no variable has that name.
    //
    // A value equal to the script's default is still recorded, rather than
    // quietly dropped. "I want this pinned at 90 whatever the code later says"
    // is a real intention, and the Inspector offers a way to undo it.
    bool SaveFieldValue(std::string_view name);

    // Forgets the saved value and puts the script's own initialiser back -
    // the value the variable had the moment the object was created, captured
    // before anything was applied to it.
    bool RevertFieldValue(std::string_view name);

    bool IsFieldOverridden(std::string_view name) const;

    // Saved values that no variable claims. Two very different causes, and
    // the Inspector tells them apart because only one is a problem:
    //
    //   - the script is not compiled in, so NOTHING is claimed and all of
    //     this is simply waiting;
    //   - the script is running and a variable has been renamed or removed,
    //     which leaves a value behind that will never be used again.
    std::vector<std::string> UnusedSavedValues() const;

    // Drops those, and says how many went. REFUSES while the script is
    // unresolved, where "unused" means "all of them" and this would be a
    // quiet way to delete everything the component was keeping safe.
    std::size_t ForgetUnusedSavedValues();

    // Points every entity reference that named `oldName` at `newName`, and
    // says how many moved. Called by Scene::RenameEntity; see the note there.
    std::size_t RetargetReferences(std::string_view oldName,
                                   std::string_view newName);

    // Does this component need to be in either update list?
    //
    // Answered separately for the two, because a script with only OnUpdate
    // must not be walked every simulation step and a script with only
    // OnFixedUpdate must not be walked every frame. A script with neither -
    // a collision-only one - is in no list at all once it has started, which
    // is the whole point of the hook table.
    //
    // Both stay true until the script has STARTED, because OnStart is fired
    // by whichever update reaches it first and something has to get there.
    bool NeedsFixedTick() const {
        return m_behaviour != nullptr && (!m_started || m_hooks.fixedUpdate != nullptr);
    }
    bool NeedsFrameTick() const {
        return m_behaviour != nullptr && (!m_started || m_hooks.update != nullptr);
    }

    // Called by ScriptSystem only.
    //
    // FixedTick runs inside a simulation step; FrameTick runs once per drawn
    // frame. Neither takes a delta any more - a script asks Time for the one
    // it wants, and gets the right answer for whichever of these it is in.
    void FixedTick();
    void FrameTick();
    void DispatchCollision(const std::string& messageType, EntityId other);

    // Used when the compiled scripts are being reloaded.
    //
    // UnbindForReload destroys the running behaviour but KEEPS the name, so
    // RebindAfterReload can find the newly compiled version of the same
    // script. That is what lets you edit a script, come back to the editor,
    // and carry on with the same scene - nothing has to be reattached.
    void UnbindForReload();
    void RebindAfterReload();

private:
    // Fires OnStart if it has not run yet. Shared by both ticks so that it
    // happens exactly once however many updates a script has.
    void EnsureStarted();

    void Bind();
    void Unbind();

    // Asks the script for its variables, remembers what they started as, and
    // then applies whatever was saved. In that order, because the defaults
    // have to be captured BEFORE anything overwrites them - they are the only
    // record of what the code said, and the object is the only place they
    // exist.
    void BuildFields();
    void ApplySavedValues();

    std::string                      m_scriptName;
    std::unique_ptr<ScriptBehaviour> m_behaviour;
    ScriptHooks                      m_hooks;
    bool                             m_started = false;

    // Rebuilt on every bind and emptied on every unbind, exactly like
    // m_hooks: both are made of pointers into the compiled script library.
    ScriptFields m_fields;

    // The values the code itself gave, captured at bind. Used by the revert
    // button, which is the only reason they are kept - the live object has
    // them until somebody changes one.
    Json m_defaults = Json::object();

    // The durable half. Loaded from the scene file, written back to it, and
    // KEPT ACROSS A RELOAD and across a script that is not compiled in - the
    // same rule m_scriptName follows, and for the same reason.
    Json m_savedValues = Json::object();

    // Set once the trigger-versus-collision mismatch has been reported for
    // this script, so the message appears once rather than sixty times a
    // second for as long as the two things overlap.
    bool                             m_warnedAboutTriggerMismatch = false;

    // The same idea for a spawner whose prefab variable nobody has set. See
    // ScriptBehaviour::Instantiate(const PrefabRef&).
    bool                             m_warnedAboutUnsetPrefab = false;

    // ScriptBehaviour sets the flag above, which is the only member of this
    // class it touches - the rest of its accessors go through the public
    // interface.
    friend class ScriptBehaviour;
};

// ---------------------------------------------------------------------------
//  Reaching another entity's SCRIPT.
//
//  EntityRef::Find<T> covers every component the engine defines - a rigidbody,
//  a sprite, a collider - because those are found by their type name and the
//  type is in a header everything can see. A script is neither:
//
//      Entity::Find<T> looks up T::kTypeName, and every script's component is
//      called "ScriptComponent". Asking for "Chaser" finds nothing however
//      many Chasers are attached.
//
//  So these walk the entity's scripts instead. The typed one asks each
//  behaviour whether it is really a T, which is a question only a compiler
//  can answer - hence dynamic_cast, the one place in the engine that uses it.
//
//  ==========================================================================
//  WHY THESE ARE HERE AND NOT IN EntityRef.h
//
//  dynamic_cast needs the COMPLETE ScriptBehaviour, and ScriptBehaviour lives
//  in this file. EntityRef.h is included BY this file - ScriptFields.h pulls
//  it in - so it cannot include this one back. The reference knows nothing
//  about scripts; scripts know about the reference.
//
//  ==========================================================================
//  THE TYPED FORM NEEDS THE CLASS TO BE NAMEABLE, which most are not.
//
//  Every script in assets/scripts sits in an `namespace { }`, so no other file
//  can mention its type at all - that is what keeps two scripts from colliding
//  and it is the right default. To point at a script's own members from
//  another script, that class has to move into a header both files include;
//  the registry already supports it (see Entry::sourceFile).
//
//  MOST REFERENCES DO NOT NEED IT. "Which entity, and does it have a Chaser on
//  it" is answered by a reference constrained to "Chaser" plus a transform or
//  a message, and that needs no headers at all.
// ---------------------------------------------------------------------------

// Any script on `entity` that is really a T, or null.
template <class T>
T* FindScriptOn(Entity& entity) {
    for (std::size_t i = 0; i < entity.ComponentCount(); ++i) {
        Component* component = entity.ComponentAt(i);
        if (component == nullptr) {
            continue;
        }
        // As TEXT, because the two literals live in engine.dll and in the
        // compiled scripts and nothing folded them to one address.
        if (std::string_view(component->TypeName()) != ScriptComponent::kTypeName) {
            continue;
        }
        if (T* found = dynamic_cast<T*>(
                static_cast<ScriptComponent*>(component)->Behaviour())) {
            return found;
        }
    }
    return nullptr;
}

// The same, through a reference - which is the spelling a script actually
// writes: FindScriptOn<Chaser>(ref).
template <class T>
T* FindScriptOn(const EntityRef& ref) {
    Entity* entity = ref.Target();
    return entity != nullptr ? FindScriptOn<T>(*entity) : nullptr;
}

// Untyped, by the name the registry knows it under. This is what the editor
// uses to check a reference's constraint, and what a script can use when it
// only needs to know whether something is there.
ScriptComponent* FindScriptNamedOn(Entity& entity, std::string_view scriptName);

// ---------------------------------------------------------------------------
//  THE SAME QUESTIONS, ACROSS A WHOLE SCENE - "where is the game-rules
//  script?"
//
//  This is the one lookup that had no spelling at all, and it is needed by the
//  most ordinary task in any game: something happens, and the object keeping
//  score has to be told. Scene::FindFirstWith<T> cannot answer it, because
//  every script's component reports its type name as "ScriptComponent" - so
//  asking for a Score finds nothing and asking for a ScriptComponent finds an
//  arbitrary one.
//
//  WHAT TO USE WHEN:
//
//    FindScriptNamedIn  needs no headers at all. It finds the component by the
//                       name the registry knows it under and hands back a
//                       ScriptComponent*, which is enough to know a thing
//                       exists - and enough to send it a message.
//
//    FindScriptIn<T>    hands back the script OBJECT, so its own members and
//                       methods can be called. It needs T to be NAMEABLE from
//                       the calling file, and most scripts are not: every
//                       script in assets/scripts sits in an `namespace { }`,
//                       which is the right default and is what stops two
//                       scripts colliding. To point at one's members from
//                       elsewhere, move that class into a header both files
//                       include. The registry already supports it - see
//                       Entry::sourceFile.
//
//  BOTH SKIP ANYTHING QUEUED FOR DESTRUCTION, because a scorekeeper that died
//  this tick is not the scorekeeper.
//
//  NEITHER CACHES. Finding one costs a walk of the scene, so a script that
//  needs it every frame should ask once in OnStart and keep the ENTITY ID -
//  never the pointer, which is replaced wholesale every time scripts are
//  rebuilt. An EntityRef variable is better still: it survives a reload, it
//  survives a save and load, and the Inspector lets somebody point it at a
//  different object without touching the code. See EntityRef.h.
// ---------------------------------------------------------------------------
ScriptComponent* FindScriptNamedIn(Scene& scene, std::string_view scriptName);

template <class T>
T* FindScriptIn(Scene& scene) {
    T* found = nullptr;
    scene.ForEachUntil([&](Entity& entity) {
        if (DeferredOps::IsPendingDestroy(entity.Id())) {
            return true;
        }
        if (T* script = FindScriptOn<T>(entity); script != nullptr) {
            found = script;
            return false;
        }
        return true;
    });
    return found;
}

// ---------------------------------------------------------------------------
//  The system that runs them, and it runs them at TWO different rates.
//
//  OnFixedUpdate goes through the System interface below, at stage 200
//  (Gameplay) - BEFORE movement at 300 and collision at 400, so a script that
//  decides to move something this step has that movement applied and checked
//  in the same step rather than the next one.
//
//  OnUpdate does NOT, and that is the one surprising thing in this file.
//  ==========================================================================
//  WHY THE PER-FRAME UPDATE IS NOT A RENDER-STAGE SYSTEM
//
//  Stages 800 and above already run once per drawn frame, via
//  SystemScheduler::RenderPass, so registering a second system up there looks
//  like the obvious way to get a per-frame hook. It is a trap.
//
//  RenderPass is called from Engine::RenderWorld, and THE EDITOR CALLS
//  RenderWorld TWICE - once for the Game view and once for the Scene view.
//  Drawing the world twice is the entire point of having two panels, so that
//  is correct for anything that draws. For a script it is a disaster: every
//  OnUpdate would run twice per frame in the editor and once in the player,
//  so anything accumulating would move at double speed in the editor only,
//  and nothing anywhere would report it.
//
//  So FrameUpdate is a plain static called once by Engine, from a place that
//  happens once per frame no matter how many times the world is drawn.
// ---------------------------------------------------------------------------
class ScriptSystem final : public System {
public:
    void        Update(float deltaSeconds) override;
    const char* Name() const override  { return "ScriptSystem"; }
    int         Order() const override { return SystemStage::kGameplay; }

    // The per-frame half. Called by Engine ONLY, exactly once per frame, and
    // only when the game is actually running - see Engine::Simulate for the
    // gate and why it is the one Unity uses.
    static void FrameUpdate();

    static void        Register(ScriptComponent& script);
    static void        Unregister(ScriptComponent& script);
    static void        Clear();
    static std::size_t Count();

    // How many of those are actually walked, per simulation step and per
    // drawn frame. Both are smaller than Count() whenever some scripts are
    // collision-only or only have one of the two updates, and that gap is the
    // point of the whole hook mechanism.
    static std::size_t TickingCount();
    static std::size_t FrameTickingCount();

    // How many attached scripts could not be found in this build. Shown in the
    // editor, because "nothing happens when I press Play" and "three of my
    // scripts are not compiled in" are the same fact, and only one of them
    // tells you what to do about it.
    static std::size_t UnresolvedCount();

    // The two halves of a script reload, applied to every attached script.
    // ScriptLibrary calls these around loading the compiled library; nothing
    // else should. See ScriptLibrary.h for the order and why it matters.
    static void UnbindAll();
    static void RebindAll();

    // Points every component using `oldName` at `newName`, and says how many
    // it moved. Used when a rebuild shows that a file which defined one class
    // now defines a differently-named one - the alternative is a scene full of
    // entities silently referring to a class that no longer exists.
    static std::size_t RebindRenamed(std::string_view oldName, std::string_view newName);

    // How many attached components are using this script name. Lets the editor
    // report a rename in terms of what it actually costs you.
    static std::size_t CountUsing(std::string_view scriptName);

    // Points every entity reference in every attached script at an entity's
    // new name. The sibling of RebindRenamed above - that one follows a script
    // CLASS being renamed, this one follows an ENTITY being renamed - and
    // called from Scene::RenameEntity rather than from the editor, because a
    // reference has to track the name wherever the rename came from.
    static std::size_t RetargetRenamed(std::string_view oldName,
                                       std::string_view newName);

    static void RegisterComponentTypes();

    // Listens for collision messages and passes them to the right behaviours.
    // Done once at start-up rather than once per component.
    static void SubscribeToCollisions();
};

} // namespace eng

// ---------------------------------------------------------------------------
//  So that ENGINE_LOG_*("{}", name) works.
//
//  Six scripts in assets/scripts wrote
//
//      Owner() != nullptr ? Owner()->Name() : "<none>"
//
//  in a log line, because that was the shortest correct spelling. These two
//  specializations are what let that be `name` instead, and they are the
//  reason <format> is included at the top of this file.
//
//  Both inherit from std::formatter<std::string>, so they inherit its parse()
//  too and format specifications work: "{:>12}" pads an entity's name the way
//  it pads any other string. Verified against /std:c++20, which is what the
//  editor compiles a project's scripts with - the engine itself is C++23, and
//  that gap is exactly where a formatter would have broken.
// ---------------------------------------------------------------------------
template <>
struct std::formatter<eng::NameHandle> : std::formatter<std::string> {
    auto format(const eng::NameHandle& handle, std::format_context& context) const {
        return std::formatter<std::string>::format(handle, context);
    }
};

template <>
struct std::formatter<eng::TagHandle> : std::formatter<std::string> {
    auto format(const eng::TagHandle& handle, std::format_context& context) const {
        return std::formatter<std::string>::format(handle, context);
    }
};
