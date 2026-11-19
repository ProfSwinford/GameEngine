#include <engine/Engine.h>

// ===========================================================================
//  Persistent - the worked example for DontDestroyOnLoad.
//
//  This entity survives a scene change as the SAME object: the counter below
//  goes on counting, because nothing was torn down and rebuilt. That is the
//  distinction from carrying state in a file-static, which is what
//  GameController.cpp does and which cannot carry anything but numbers.
//
//  THE SINGLETON CHECK IS NOT OPTIONAL. Load a scene that has its own copy of
//  this and there are now two, exactly as in Unity. The name goes on meaning
//  the one that survived, so the newcomer finds it and stands down.
// ===========================================================================
class Persistent final : public ScriptBehaviour {
public:
    float secondsAlive = 0.0f;   // kept across scene changes

    void OnStart() {
        if (FindEntity("Persistent") != gameObject) {
            Debug::Log("a second Persistent appeared and stood down");
            DestroySelf();
            return;
        }

        DontDestroyOnLoad();
        Debug::Log("Persistent will now survive scene changes");
    }

    void OnFixedUpdate() {
        secondsAlive += Time::FixedDeltaTime();
    }

    void OnDestroy() {
        Debug::Log("Persistent destroyed after {} seconds", secondsAlive);
    }
};

ENGINE_REGISTER_SCRIPT(Persistent)
