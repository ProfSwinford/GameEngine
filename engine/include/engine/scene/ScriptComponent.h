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
//              Transform()->Translate({0, Time::DeltaTime() * 50});
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
#include <engine/scene/ScriptFields.h>
#include <engine/scene/ScriptHooks.h>
#include <engine/scene/SystemOrder.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
namespace eng {

class ScriptComponent;

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

    Entity*      Owner() const;
    EntityId     OwnerId() const;
    Scene*       GetScene() const;
    Transform2D* Transform() const;

private:
    friend class ScriptComponent;
    ScriptComponent* m_component = nullptr;
};

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
