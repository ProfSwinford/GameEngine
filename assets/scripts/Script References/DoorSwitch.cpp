// =============================================================================
//  DoorSwitch - stand on the plate, the door it points at opens.
//
//  THE WORKED EXAMPLE FOR eng::ScriptRef<T>: a reference to another SCRIPT, as
//  opposed to a component the engine defines. Drag the door from the Hierarchy
//  onto the `door` slot in the Inspector and this calls its own method.
//
//  OPEN assets/scenes/script_references.oscene to see it running. Two plates,
//  two doors, and each plate opens the one door it was pointed at - swap the
//  two `door` values in the Inspector and they open the other way round, with
//  nothing recompiled.
//
//  WHY A REFERENCE AND NOT A MESSAGE. This is one specific door, chosen once,
//  by the person building the level - so it is a slot somebody drags, and a
//  broadcast would open every door in the scene. The opposite case is in
//  Bullet.cpp: a bullet is spawned from a prefab at runtime, so nobody could
//  have dragged a scorekeeper onto it, and the payload IS the whole content -
//  there is no method to call. That one is correctly a message.
//
//  AND NOT A POINTER. `Door* door;` could never be filled in: the Inspector
//  matters in EDIT mode, where no script ticks and OnStart never runs, and a
//  pointer has no form in a scene file so the drop would have nowhere to be
//  saved. See engine/include/engine/scene/ScriptRef.h for all four reasons.
// =============================================================================

#include <engine/Engine.h>

// NO #include FOR Door, AND THAT IS THE POINT. The build puts every script in
// the project in scope before this file is compiled, so another script's name
// is simply available - see GenerateScriptUnit in engine/tools/ScriptVars.h.

class DoorSwitch final : public eng::ScriptBehaviour {
public:
    // THE BARE SCRIPT NAME IS A REFERENCE. `Door door;` reads like Unity's
    // `public Door door;` and behaves like one: the Inspector offers only
    // entities that actually carry a Door, paints the name red if it stops
    // resolving, and follows a rename - all inherited from eng::EntityRef.
    //
    // It is not a Door BY VALUE. The bare name is an alias for a reference to
    // one, so this costs an entity name rather than a whole Door, and it is
    // null rather than dangling when the far entity goes away. The arrow is
    // the one visible difference from Unity: C++ has no operator. to overload.
    Door door;                   // drag the door this plate opens

    bool openOnce = true;        // or open and close as things come and go

    // WHAT ANOTHER SCRIPT WOULD ASK THIS ONE. Any script holding a
    // `DoorSwitch plate;` can call `plate->OpensOnce()` - an ordinary method
    // on an ordinary class, reached through the reference exactly as this
    // file reaches Door::Open below.
    //
    // NOT BY Door, THOUGH, and the note on its `doorSwitch` member says why:
    // this file calls into Door, so Door is compiled first, and a class that
    // is compiled first cannot call back. Pointing both ways is free; calling
    // both ways is the one thing C++ will not do.
    bool OpensOnce() const { return openOnce; }

    void OnTriggerEnter(eng::EntityId) {
        // `if (door)` is true only when it resolves RIGHT NOW - not "was
        // something dropped on it", which is door.IsSet(). An unset slot, a
        // deleted door and an entity with no Door on it are all the same
        // answer here, and all handled the same way.
        if (!door) {
            eng::Debug::LogWarningOnce("{}: no door is set on this switch", Name());
            return;
        }
        door->Open();
    }

    void OnTriggerExit(eng::EntityId) {
        if (door && !openOnce) {
            door->Close();
        }
    }
};

ENGINE_REGISTER_SCRIPT(DoorSwitch)
