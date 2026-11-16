#include <engine/input/InputActions.h>

#include <engine/core/Log.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace eng {

namespace {

// ---------------------------------------------------------------------------
//  EVERY CONTROL CURRENTLY HELD. A vector and not a set: a hand holds three
//  or four keys at the very most, a linear scan over four is faster than
//  hashing one, and the order is stable for anything that wants to look.
//
//  In a function rather than at file scope, because the engine DLL exports
//  functions and not data - the same note TileAsset.cpp carries.
// ---------------------------------------------------------------------------
std::vector<Control>& Held() {
    static std::vector<Control> held;
    return held;
}

void Press(const Control& control) {
    std::vector<Control>& held = Held();
    if (std::find(held.begin(), held.end(), control) == held.end()) {
        held.push_back(control);
    }
}

void Release(const Control& control) {
    std::vector<Control>& held = Held();
    const auto            it   = std::find(held.begin(), held.end(), control);
    if (it != held.end()) {
        held.erase(it);
    }
}

// ONE PLACE WHERE AN EVENT MEANS SOMETHING, so the span overload and the pump
// overload cannot drift apart and the tested path is the shipped path.
void Apply(const RawEvent& event) {
    switch (event.kind) {
        case RawEventKind::KeyDown:
            Press(Control{ControlDevice::Key, event.code});
            break;
        case RawEventKind::KeyUp:
            Release(Control{ControlDevice::Key, event.code});
            break;
        case RawEventKind::MouseButtonDown:
            Press(Control{ControlDevice::MouseButton, event.code});
            break;
        case RawEventKind::MouseButtonUp:
            Release(Control{ControlDevice::MouseButton, event.code});
            break;

        // ALT-TAB IS HOW A KEY GETS STUCK. The KeyUp goes to whichever window
        // took the focus and never arrives here, so the only safe reading of
        // "this window stopped being the one being typed into" is that
        // nothing is held any more.
        case RawEventKind::WindowFocusLost:
            Held().clear();
            break;

        default:
            break;
    }
}

float Amount(const Control& control) {
    return InputActions::IsDown(control) ? 1.0f : 0.0f;
}

// What one binding reads right now, always as a vector so the three shapes
// have one return type. A single control is (1, 0) or (0, 0).
Vec2 BindingVector(const InputBinding& binding) {
    switch (binding.shape) {
        case InputBinding::Shape::Control:
            return Vec2{Amount(binding.control), 0.0f};

        case InputBinding::Shape::Vector2Composite: {
            const Vec2 raw{Amount(binding.right) - Amount(binding.left),
                           Amount(binding.up) - Amount(binding.down)};

            // DigitalNormalized is the default, here and in Unity: holding up
            // and right at once is (1, 1), about 1.41 long, so a diagonal
            // would be 41% faster than a straight line. Normalized() already
            // answers (0, 0) for a zero-length vector, so "no keys held"
            // needs no special case. This is the same correction
            // InputMap::GetAxis2D applies, deliberately - the two halves of
            // the engine must not disagree about what a diagonal is worth.
            if (binding.mode == CompositeMode::DigitalNormalized) {
                return raw.Normalized();
            }
            return raw;
        }

        case InputBinding::Shape::AxisComposite:
            return Vec2{Amount(binding.positive) - Amount(binding.negative), 0.0f};
    }
    return Vec2{};
}

// How much a reading counts as, for deciding which binding wins.
float Magnitude(const Vec2& value, ControlType type) {
    return (type == ControlType::Vector2) ? value.Length() : std::abs(value.x);
}

} // namespace

const char* ToString(ActionPhase phase) {
    switch (phase) {
        case ActionPhase::Started:   return "Started";
        case ActionPhase::Performed: return "Performed";
        case ActionPhase::Canceled:  return "Canceled";
    }
    return "?";
}

// ---------------------------------------------------------------------------

void InputActions::Sample(std::span<const RawEvent> events) {
    for (const RawEvent& event : events) {
        Apply(event);
    }
}

void InputActions::Sample(const EventPump& pump) {
    for (std::size_t i = 0; i < pump.Count(); ++i) {
        // Anything the editor's GUI claimed never reaches the game, exactly
        // as InputMap skips the same events.
        if (pump.WasConsumed(i)) {
            continue;
        }
        Apply(pump.At(i));
    }
}

void InputActions::ReleaseAll() { Held().clear(); }

bool InputActions::IsDown(const Control& control) {
    if (!control) {
        return false;
    }
    const std::vector<Control>& held = Held();
    return std::find(held.begin(), held.end(), control) != held.end();
}

std::size_t InputActions::DownCount() { return Held().size(); }

// ---------------------------------------------------------------------------

Vec2 EvaluateAction(const InputAction& action) {
    // THE LOUDEST BINDING WINS, which is Unity's conflict resolution and is
    // what makes WASD and the arrow keys both work on one action without
    // adding up: holding W and Up gives (0,1) rather than (0,2).
    Vec2  best{};
    float bestMagnitude = 0.0f;

    for (const InputBinding& binding : action.bindings) {
        const Vec2  value     = BindingVector(binding);
        const float magnitude = Magnitude(value, action.controlType);
        if (magnitude > bestMagnitude) {
            bestMagnitude = magnitude;
            best          = value;
        }
    }

    // A BUTTON IS ON OR OFF whatever it was bound to. A composite dragged
    // onto a Button action is odd but not an error, and this is what it
    // means: anything at all is 1.
    if (action.controlType == ControlType::Button) {
        return Vec2{bestMagnitude > 0.0f ? 1.0f : 0.0f, 0.0f};
    }
    return best;
}

std::string ActionHandlerMethodName(std::string_view action) {
    std::string name;
    name.reserve(action.size() + 2);

    for (const char c : action) {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit  = c >= '0' && c <= '9';
        if (letter || digit || c == '_') {
            name.push_back(c);
        }
    }

    // Nothing usable. The caller says so rather than inventing a name, which
    // is where this parts company with Unity - that one throws from inside
    // the input system on an action named "!!!", with a stack trace that
    // names none of the three places anybody would look.
    if (name.empty()) {
        return name;
    }

    // A leading digit cannot start an identifier.
    if (name[0] >= '0' && name[0] <= '9') {
        name.insert(name.begin(), '_');
    }

    // ONLY THE FIRST CHARACTER, which is Unity's rule and not a typo for
    // "capitalise each word": the action "move camera" becomes OnMovecamera
    // there, so it does here. Somebody reading Unity's documentation and
    // naming their actions accordingly gets the methods they were promised.
    if (name[0] >= 'a' && name[0] <= 'z') {
        name[0] = static_cast<char>(name[0] - 'a' + 'A');
    }

    return "On" + name;
}

// ---------------------------------------------------------------------------

namespace {

// The scalar reading of a vector one. For a Vector2 action that is HOW FAR
// the stick is pushed, which is the useful scalar; for everything else the
// value simply is x.
float ScalarOf(const Vec2& value, ControlType type) {
    return (type == ControlType::Vector2) ? value.Length() : value.x;
}

bool IsActuated(const Vec2& value) { return value != Vec2{}; }

} // namespace

ActionMapState::~ActionMapState() {
    // THE ONE THING THIS DESTRUCTOR DOES. See the header: a Deliver in
    // progress further down the stack is about to carry on using a state that
    // no longer exists, and this is how it finds out not to.
    if (m_alive != nullptr) {
        *m_alive = false;
    }
}

bool ActionMapState::Deliver(const ActionEvent& event) const {
    // DISPATCHED FROM A COPY, because a handler is allowed to subscribe or
    // unsubscribe - a script that switches maps on OnPause is the obvious
    // case - and would otherwise invalidate the walk underneath itself. The
    // copy costs an allocation on frames where something happened, which is a
    // few frames a second, and nothing at all on the rest.
    const std::vector<Subscriber> snapshot = m_subscribers;

    // AND THE LIVENESS HANDLE IS TAKEN HERE TOO, on this stack, while `this`
    // is still known to exist - which is the whole trick. A handler below may
    // destroy the component this state belongs to; afterwards `m_alive` can no
    // more be read than any other member, but this copy keeps the flag itself
    // alive and readable.
    const std::shared_ptr<bool> alive = m_alive;

    for (const Subscriber& subscriber : snapshot) {
        // CHECKED BETWEEN SUBSCRIBERS AND NOT ONLY AT THE END, because the
        // snapshot outlives the state but the CALLBACKS IN IT DO NOT. The
        // routing lambda captures `this`, so a subscriber that destroyed the
        // component would be followed by a call to Route() through a dangling
        // pointer - and a user subscriber added before the first dispatch sits
        // ahead of the routing one in this list, which is the ordinary case
        // rather than a contrived one.
        //
        // The rest of the list hearing nothing is the right answer and not a
        // compromise: the thing the event is about has just ceased to exist.
        if (alive != nullptr && !*alive) {
            return false;
        }
        if (subscriber.callback) {
            subscriber.callback(event);
        }
    }

    return alive == nullptr || *alive;
}

bool ActionMapState::CancelEverything() {
    if (m_map == nullptr) {
        return true;
    }
    const std::vector<InputAction>& actions = m_map->Actions();
    for (std::size_t i = 0; i < actions.size() && i < m_last.size(); ++i) {
        if (!IsActuated(m_last[i].vector)) {
            continue;
        }
        m_last[i] = Reading{};
        if (actions[i].type != ActionType::PassThrough) {
            if (!Deliver(ActionEvent{actions[i].name, ActionPhase::Canceled, 0.0f,
                                     Vec2{}, actions[i].type})) {
                // A handler destroyed us. `actions`, `m_last` and `m_map` are
                // all gone; the next loop condition alone would be enough to
                // read freed storage.
                return false;
            }
        }
    }
    return true;
}

bool ActionMapState::SetMap(const ActionMap* map) {
    if (map == m_map) {
        return true;
    }

    // Everything the OLD map had running is told to stop, so a script that
    // started something on Pause hears about the end of it.
    if (!CancelEverything()) {
        // AND ONE OF THOSE HANDLERS DESTROYED US. Assigning m_map below would
        // be a write through a dangling `this`, which is worse than the read
        // it replaced.
        return false;
    }

    m_map = map;
    m_last.clear();
    if (m_map == nullptr) {
        return true;
    }

    const std::vector<InputAction>& actions = m_map->Actions();
    m_last.assign(actions.size(), Reading{});

    // THE INITIAL STATE CHECK, which is Unity's and is two different answers
    // on purpose.
    //
    //   A VALUE action adopts whatever is already held, so swapping to a map
    //   that binds W to Move while W is down gives Move (0,1) at once instead
    //   of nothing until you let go and press again. Left at zero here, which
    //   makes the next Dispatch see 0 -> (0,1) and raise it naturally - there
    //   is no second code path for the first frame.
    //
    //   A BUTTON does not, and the reason is concrete: Escape opens the menu,
    //   the menu's map binds Escape to Cancel, and an initial state check
    //   would close the menu with the same press that opened it. Recording
    //   what is held means the button cannot fire until it has been let go
    //   of, because an edge needs the value to reach zero first.
    for (std::size_t i = 0; i < actions.size(); ++i) {
        if (actions[i].type != ActionType::Button) {
            continue;
        }
        if (IsActuated(EvaluateAction(actions[i]))) {
            m_last[i] = Reading{0.0f, Vec2{}, true};
        }
    }
    return true;
}

void ActionMapState::Reset() {
    m_last.assign(m_map == nullptr ? 0 : m_map->Actions().size(), Reading{});
}

void ActionMapState::Dispatch() {
    if (m_map == nullptr) {
        return;
    }

    // THE MAP THIS WALK BELONGS TO, remembered because a handler is allowed
    // to leave it. See the loop condition below.
    const ActionMap* const          entered = m_map;
    const std::vector<InputAction>& actions = m_map->Actions();

    // The map's contents can change under us when its asset is reloaded while
    // the editor is open, so the parallel array is sized here rather than
    // only when the map is set.
    if (m_last.size() != actions.size()) {
        m_last.assign(actions.size(), Reading{});
    }

    // ======================================================================
    //  THE MAP TEST COMES FIRST, AND IS NOT A TIDINESS CHECK.
    //
    //  Deliver below runs SCRIPT CODE, and a handler may switch maps - a
    //  PauseMenu whose OnPause calls SwitchCurrentActionMap is the pattern
    //  the docs advertise, so this is the expected case and not an abuse.
    //  SetMap then re-sizes m_last to the NEW map while this walk is still
    //  holding `actions` from the OLD one, and the next iteration indexes
    //  m_last past its end. With the shipped assets/input/player.oinput
    //  (Gameplay has more actions than Menu, and Pause is not the last of
    //  them) that is a read off the end on the very next step: MSVC debug
    //  fails fast with "vector subscript out of range".
    //
    //  AND IT IS TESTED BEFORE i < actions.size() ON PURPOSE, because
    //  `actions` is itself a reference into the map being left. If the
    //  handler reloaded the asset, that vector is gone and even asking for
    //  its size is undefined - so the short-circuit has to happen before
    //  anything touches it again.
    //
    //  STOPPING IS ALSO THE RIGHT ANSWER rather than merely the safe one.
    //  Unity ends delivery for a map at the moment something switches away
    //  from it, and SetMap has already raised Canceled for everything the
    //  old map had in flight, so nothing is left believing a control is
    //  still held. The new map starts clean on the next frame.
    //
    //  i < m_last.size() is belt-and-braces: while m_map is unchanged the
    //  two sizes agree, but a reload that happened to re-use this address
    //  would slip past the pointer test, and that is not worth a crash.
    //
    //  ======================================================================
    //  AND A SECOND HAZARD THE MAP TEST CANNOT SEE: THIS STATE DYING.
    //
    //  A handler may destroy the component this state is a member of, and
    //  then even `m_map == entered` is a read through a dangling `this`. So
    //  every Deliver below is checked - that is what its bool answers - and
    //  the walk returns at once, touching nothing. The checks are at each
    //  Deliver rather than at the top of the loop because Started and
    //  Performed are delivered back to back, and the first of them is enough
    //  to free us. See the header's note on the destructor.
    // ======================================================================
    for (std::size_t i = 0;
         m_map == entered && i < actions.size() && i < m_last.size(); ++i) {
        const InputAction& action = actions[i];

        const Vec2  nowVector = EvaluateAction(action);
        const float nowValue  = ScalarOf(nowVector, action.controlType);

        // STILL WAITING FOR THE CONTROL TO BE LET GO OF. It was already held
        // when this map was switched to, so it belongs to whatever the player
        // was doing before and not to this map. Nothing is raised and nothing
        // is readable until it drops, at which point the action becomes
        // ordinary again and the next genuine press fires.
        if (m_last[i].suppressed) {
            if (!IsActuated(nowVector)) {
                m_last[i].suppressed = false;
            }
            continue;
        }

        const Vec2 wasVector = m_last[i].vector;
        if (nowVector == wasVector) {
            continue;
        }
        m_last[i] = Reading{nowValue, nowVector, false};

        // PASS THROUGH HAS NO PHASES, which is the whole of what distinguishes
        // it from Value: every change gets through as a Performed, including
        // the one back to zero, and nothing is ever Started or Canceled.
        if (action.type == ActionType::PassThrough) {
            if (!Deliver(ActionEvent{action.name, ActionPhase::Performed, nowValue,
                                     nowVector, action.type})) {
                return;
            }
            continue;
        }

        const bool wasActuated = IsActuated(wasVector);
        const bool isActuated  = IsActuated(nowVector);

        if (!wasActuated && isActuated) {
            // Both phases in the same breath. The header says why they are
            // not one phase even though nothing separates them yet.
            //
            // AND THE GAP BETWEEN THESE TWO IS THE NARROWEST PLACE THE OTHER
            // HAZARD LIVES. A handler on Started that removes the component -
            // `gameObject->RemoveComponent("PlayerInputComponent")`, which is
            // immediate - has destroyed this state before the second Deliver
            // is even reached, and that second call would copy m_subscribers
            // out of freed storage. Checking only at the top of the loop
            // would not catch it.
            if (!Deliver(ActionEvent{action.name, ActionPhase::Started, nowValue,
                                     nowVector, action.type})) {
                return;
            }
            if (!Deliver(ActionEvent{action.name, ActionPhase::Performed, nowValue,
                                     nowVector, action.type})) {
                return;
            }
        } else if (wasActuated && isActuated) {
            // Still going, at a different amount. A Button cannot reach here
            // - it has only two values - which is why a held button raises
            // nothing, exactly as Unity's does.
            if (!Deliver(ActionEvent{action.name, ActionPhase::Performed, nowValue,
                                     nowVector, action.type})) {
                return;
            }
        } else if (wasActuated && !isActuated) {
            if (!Deliver(ActionEvent{action.name, ActionPhase::Canceled, nowValue,
                                     nowVector, action.type})) {
                return;
            }
        }
    }
}

// ---------------------------------------------------------------------------

const ActionMapState::Reading* ActionMapState::ReadingFor(std::string_view action) const {
    if (m_map == nullptr) {
        return nullptr;
    }
    const std::vector<InputAction>& actions = m_map->Actions();
    for (std::size_t i = 0; i < actions.size() && i < m_last.size(); ++i) {
        if (actions[i].name == action) {
            return &m_last[i];
        }
    }
    // An action this map does not have reads zero rather than being an error,
    // for the same reason InputMap answers Idle for an unbound name: once
    // maps are swappable, a script asking about an action the CURRENT map
    // happens not to bind is the ordinary case and not a mistake.
    return nullptr;
}

float ActionMapState::Value(std::string_view action) const {
    const Reading* reading = ReadingFor(action);
    return (reading == nullptr) ? 0.0f : reading->value;
}

Vec2 ActionMapState::Vector(std::string_view action) const {
    const Reading* reading = ReadingFor(action);
    return (reading == nullptr) ? Vec2{} : reading->vector;
}

bool ActionMapState::IsDown(std::string_view action) const {
    return IsActuated(Vector(action));
}

ActionMapState::Token ActionMapState::Subscribe(Callback callback) {
    if (!callback) {
        return 0;
    }
    const Token token = m_nextToken++;
    m_subscribers.push_back(Subscriber{token, std::move(callback)});
    return token;
}

void ActionMapState::Unsubscribe(Token token) {
    if (token == 0) {
        return;
    }
    const auto it = std::find_if(m_subscribers.begin(), m_subscribers.end(),
                                 [token](const Subscriber& s) { return s.token == token; });
    if (it != m_subscribers.end()) {
        m_subscribers.erase(it);
    }
}

void ActionMapState::ClearSubscribers() { m_subscribers.clear(); }

} // namespace eng
