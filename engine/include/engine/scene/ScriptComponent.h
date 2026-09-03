#pragma once

// ============================================================================
//  ScriptComponent.h - writing your own behaviour for an entity.
//
//  This is the engine's version of a Unity MonoBehaviour. You write a class,
//  give it whichever lifecycle functions you actually want, and it runs:
//
//      class Bouncer : public eng::ScriptBehaviour {
//      public:
//          void OnUpdate(float dt) { Transform()->Translate({0, dt * 50}); }
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
//  build time which hooks a script actually has, a script with no OnUpdate is
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
#include <engine/scene/ScriptHooks.h>
#include <engine/scene/SystemOrder.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace eng {

class ScriptComponent;

// ---------------------------------------------------------------------------
//  The class you inherit from.
//
//  Note what is NOT here: OnStart, OnUpdate, OnDestroy, OnCollisionEnter. If
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
};

// ---------------------------------------------------------------------------
//  The component that holds a script.
// ---------------------------------------------------------------------------
class ScriptComponent final : public Component {
public:
    static constexpr const char* kTypeName = "ScriptComponent";

    ~ScriptComponent() override;

    const char* TypeName() const override { return kTypeName; }

    // Scene file field:
    //   "script": "PlayerController"
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

    // Which hooks the bound script turned out to have. The Inspector shows
    // them, because "this script has no OnUpdate" is the answer to "why is
    // nothing happening" often enough to be worth putting on screen.
    const ScriptHooks& Hooks() const { return m_hooks; }

    // Does this component need Tick() at all? False for a script that has
    // already started and has no OnUpdate - and the system drops those from
    // its list rather than calling into nothing every step.
    bool NeedsTick() const {
        return m_behaviour != nullptr && (!m_started || m_hooks.update != nullptr);
    }

    // Called by ScriptSystem only.
    void Tick(float deltaSeconds);
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
    void Bind();
    void Unbind();

    std::string                      m_scriptName;
    std::unique_ptr<ScriptBehaviour> m_behaviour;
    ScriptHooks                      m_hooks;
    bool                             m_started = false;
};

// ---------------------------------------------------------------------------
//  The system that runs them.
//
//  Stage 200 (Gameplay) - BEFORE movement at 300 and collision at 400, so a
//  script that decides to move something this step has that movement applied
//  and checked in the same step rather than the next one.
// ---------------------------------------------------------------------------
class ScriptSystem final : public System {
public:
    void        Update(float deltaSeconds) override;
    const char* Name() const override  { return "ScriptSystem"; }
    int         Order() const override { return SystemStage::kGameplay; }

    static void        Register(ScriptComponent& script);
    static void        Unregister(ScriptComponent& script);
    static void        Clear();
    static std::size_t Count();

    // How many of those are actually ticked every step. Always smaller than
    // Count() whenever some scripts are collision-only, and the gap is the
    // point of the whole hook mechanism.
    static std::size_t TickingCount();

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

    static void RegisterComponentTypes();

    // Listens for collision messages and passes them to the right behaviours.
    // Done once at start-up rather than once per component.
    static void SubscribeToCollisions();
};

} // namespace eng
