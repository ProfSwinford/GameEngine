// =============================================================================
//  DoorSwitch - stand on the plate, the door it points at opens.
//
//  THE WORKED EXAMPLE FOR eng::ScriptRef<T>: a reference to another SCRIPT, as
//  opposed to a component the engine defines. Drag the door from the Hierarchy
//  onto the `door` slot in the Inspector and this calls its own method.
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

#include "Door.h"

class DoorSwitch final : public eng::ScriptBehaviour {
public:
    // The slot. The Inspector offers only entities that actually carry a Door,
    // paints the name red if it stops resolving, and follows a rename - all of
    // it inherited from eng::EntityRef, which is what a ScriptRef is.
    ScriptRef<Door> door;        // drag the door this plate opens

    bool openOnce = true;        // or open and close as things come and go

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
