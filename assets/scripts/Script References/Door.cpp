// =============================================================================
//  Door - one door that another script can open.
//
//  NO HEADER, AND THAT IS THE POINT. The class lives here, in the .cpp, beside
//  its own ENGINE_REGISTER_SCRIPT - and DoorSwitch.cpp still writes
//
//      Door door;
//
//  with no #include of any kind. The build puts every script in the project in
//  scope before a script is compiled, so another script's name is simply
//  available. See GenerateScriptUnit in engine/include/engine/tools/ScriptVars.h
//  for how, and engine/include/engine/scene/ScriptRef.h for what the bare name
//  actually means.
//
//  A HEADER STILL WORKS. Put the class in Door.h and include it from here and
//  nothing breaks: the build folds that header into this script's namespace, so
//  the class ends up in exactly the same place. The header is now a choice
//  about how you like to organize your files rather than something the engine
//  makes you do.
//
//  METHODS GO IN THE CLASS, or `inline`. Each script still compiles into its
//  own library and nothing exports a script's symbols, so a body left in some
//  other .cpp would compile and then fail to link.
// =============================================================================

#include <engine/Engine.h>

class Door final : public eng::ScriptBehaviour {
public:
    // How far up the door slides. An ordinary exposed variable - a script that
    // other scripts point at gets its Inspector row like any other.
    float openHeight = 2.0f;   // [0..400] world units

    // POINTING BACK AT THE SCRIPT THAT POINTS AT THIS ONE. DoorSwitch.cpp has
    // a `Door door;` and this has a `DoorSwitch doorSwitch;`: the two scripts
    // refer to each other, with no header, no include and no order between
    // them. A reference needs the other script DECLARED rather than finished,
    // and the build declares every script in the project before it defines
    // any of them, so a mutual pair is no harder than a one-way one.
    //
    // THE ONE THING A PAIR CANNOT BOTH DO IS CALL. `doorSwitch->OpensOnce()`
    // is ordinary member lookup in DoorSwitch and needs that class FINISHED,
    // so the caller has to be compiled after it - and DoorSwitch calls
    // `door->Open()`, so it has to come after this one. Both cannot be last.
    // That is C++ rather than this engine: two classes whose inline methods
    // call each other cannot be written in one file either. So the plate does
    // the calling, this keeps the slot, and the build says so by name if
    // anybody writes the loop.
    DoorSwitch doorSwitch;     // the plate that opens this door

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

        // A DOOR NOBODY CAN OPEN IS A LEVEL-BUILDING MISTAKE, and the
        // reference is what makes it answerable: IsSet() asks whether
        // somebody filled the slot in, and `if (doorSwitch)` asks whether it
        // resolves to a DoorSwitch RIGHT NOW. Both work on a reference
        // pointing either way, because both go through ScriptRef's own
        // members rather than DoorSwitch's.
        if (!doorSwitch.IsSet()) {
            eng::Debug::LogWarningOnce(
                "{}: no plate is set on this door, so nothing will open it", Name());
        } else if (!doorSwitch) {
            eng::Debug::LogWarningOnce("{}: its plate has gone", Name());
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

ENGINE_REGISTER_SCRIPT(Door)
