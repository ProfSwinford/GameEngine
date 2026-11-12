// =============================================================================
//  Door - one door that another script can open.
//
//  THE CLASS IS IN A HEADER, and that is the only reason this file exists.
//  Scripts share the global namespace, so nothing hides the name `Door` - but
//  DoorSwitch.cpp still has no DECLARATION of a class defined in Door.cpp, and
//  C++ needs a complete type to call a method on one. A header both files
//  include is what supplies it.
//
//  So the pair is:
//
//      Door.h          this file - the class, and nothing else
//      Door.cpp        #include "Door.h", and ENGINE_REGISTER_SCRIPT(Door)
//      DoorSwitch.cpp  #include "Door.h", and a ScriptRef<Door>
//
//  THE REGISTRAR GOES IN THE .cpp, in one file of its own. Put it here instead
//  and it registers once per file that includes this header - which works, but
//  then which library owns the class depends on load order. One .cpp means one
//  owner. (Putting it in BOTH is a redefinition error, so at least that
//  mistake is loud.)
//
//  EVERY METHOD IS DEFINED IN THE CLASS, deliberately. Each script compiles
//  into its own library and nothing exports a script's symbols, so a body left
//  in Door.cpp would compile and then fail to link from DoorSwitch's library.
//  In-class or `inline` is the house answer.
//
//  See engine/include/engine/scene/ScriptRef.h.
// =============================================================================

#pragma once

#include <engine/Engine.h>

class Door final : public eng::ScriptBehaviour {
public:
    // How far up the door slides. An ordinary exposed variable - a script
    // whose class lives in a header gets its Inspector row like any other.
    float openHeight = 2.0f;

    bool IsOpen() const { return m_open; }

    // A LIFECYCLE HOOK IS STILL REQUIRED, even though everything interesting
    // here is called by somebody else. A class with no OnStart, OnUpdate,
    // OnFixedUpdate, OnDestroy or collision hook is refused at compile time -
    // "nothing would ever call it" - and OnExpose does not count, because it
    // only lists variables. Worth knowing before writing a script that exists
    // purely to be driven by another one: give it the hook it genuinely wants.
    void OnStart() {
        if (transform != nullptr) {
            m_shut = transform->LocalPosition();
        }
    }

    void Open() {
        if (m_open || transform == nullptr) {
            return;
        }
        m_open = true;
        transform->SetLocalPosition(m_shut + eng::Vec2{0.0f, openHeight});
        eng::Debug::Log("{} opened", Name());
    }

    void Close() {
        if (!m_open || transform == nullptr) {
            return;
        }
        m_open = false;
        transform->SetLocalPosition(m_shut);
    }

private:
    // SAFE AS PRIVATE MEMBERS, for the reason a reference across entities is
    // not: these and the behaviour object are destroyed together. Private also
    // keeps them out of the Inspector, which is right - where the door started
    // is bookkeeping, not level data.
    bool      m_open = false;
    eng::Vec2 m_shut{};
};
