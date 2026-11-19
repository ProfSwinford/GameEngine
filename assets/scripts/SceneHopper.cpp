#include <engine/Engine.h>

// A test harness: changes scene once, two seconds in. Pairs with
// Persistent.cpp to show that DontDestroyOnLoad carries an entity across.
class SceneHopper final : public ScriptBehaviour {
public:
    float afterSeconds = 2.0f;
    std::string nextScene = "scenes/tilemap.oscene";

    void OnFixedUpdate() {
        if (m_done) {
            return;
        }
        m_elapsed += Time::FixedDeltaTime();
        if (m_elapsed >= afterSeconds) {
            m_done = true;
            Debug::Log("SceneHopper is changing scene now");
            Engine::Get().RequestScene(nextScene);
        }
    }

private:
    float m_elapsed = 0.0f;
    bool  m_done    = false;
};

ENGINE_REGISTER_SCRIPT(SceneHopper)
