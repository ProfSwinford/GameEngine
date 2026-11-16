#include <engine/Engine.h>

class TestScript final : public eng::ScriptBehaviour {
public:
    bool found = false;

    void OnStart() {
    }

private:

};


// Registers the name "TestScript" so that a scene file and the editor can find it.
// WITHOUT THIS LINE the file compiles and the script can never be attached.
ENGINE_REGISTER_SCRIPT(TestScript)
