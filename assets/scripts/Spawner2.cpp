#include <engine/Engine.h>

// ===========================================================================
//  Spawner2 - the worked example for a GameObject variable.
//
//  `thing` takes EITHER a prefab from the Assets panel or an object already
//  in the scene from the Hierarchy, and Instantiate does the right thing with
//  whichever it is: a prefab is built from its file, a scene object is
//  CLONED. Nothing in this file knows or cares which was dragged onto it.
//
//  THAT IS THE POINT OF THE TYPE. Choosing between "spawn a bullet prefab"
//  and "copy the crate already sitting over there" used to mean editing this
//  file, because eng::PrefabRef and eng::EntityRef are different types. The
//  decision belongs in the Inspector, where you can change your mind by
//  dragging something else.
//
//  Note the `= nullptr`: GameObject used to be a pointer typedef and that
//  spelling still compiles, because the type converts to eng::Entity* and
//  has operator->. See scene/GameObjectRef.h.
// ===========================================================================
class Spawner2 final : public ScriptBehaviour {
public:
    GameObject thing = nullptr;

    float everySeconds = 1.0f;

    void OnFixedUpdate() {
        m_elapsed += Time::FixedDeltaTime();
        if (m_elapsed < everySeconds) {
            return;
        }
        m_elapsed = 0.0f;

        Instantiate(thing, SpawnOptions{}.At(Transform()->WorldPosition()));
    }

private:
    float m_elapsed = 0.0f;
};

ENGINE_REGISTER_SCRIPT(Spawner2)
