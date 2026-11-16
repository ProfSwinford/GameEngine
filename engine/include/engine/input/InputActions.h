#pragma once

// ============================================================================
//  InputActions.h - the EVENT-DRIVEN half of the input system.
//
//  InputMap answers questions. This one asks them for you:
//
//      // polling, which already worked and still does
//      void OnFixedUpdate() {
//          if (Input::IsPressed("Jump")) { Jump(); }
//      }
//
//      // event-driven, which is what this header leads to
//      void OnJump() { Jump(); }
//
//  Unity ships both workflows side by side, and for the reason this does:
//  polling reads better when input drives one character every frame, and
//  events read better when the input is occasional, or has to reach several
//  places at once, or belongs to a script with no other reason to run.
//
//  ==========================================================================
//  TWO LAYERS, AND THE SPLIT IS THE WHOLE DESIGN
//
//    InputActions   WHICH CONTROLS ARE PHYSICALLY DOWN. One keyboard and one
//                   mouse, so one of these, global. Fed from the raw events.
//
//    ActionMapState WHAT THAT MEANS. Holds one action map and the last value
//                   it reported for each of its actions, works out this
//                   frame's values from the physical set, and raises what
//                   changed. One per PlayerInput, because two players holding
//                   two different maps is the thing Unity's component is for
//                   and a file-scope value could not express it.
//
//  ==========================================================================
//  SAMPLING IS UNGATED. DISPATCH IS GATED. THEY ARE NOT THE SAME CALL.
//
//  This is the one piece of ordering that is not obvious and cannot be got
//  wrong quietly.
//
//  Sample() runs EVERY frame, from Engine::BeginFrame, whatever else is
//  happening - paused, edit mode, a time scale of zero. Dispatch() runs only
//  when scripts run.
//
//  WHY THEY CANNOT BE ONE CALL: EventPump::Poll clears its list every frame,
//  so an event nobody read is gone for good. Gate the sampling and this
//  happens:
//
//      hold W  ->  press Pause  ->  let go of W  ->  press Resume
//
//  The KeyUp arrived while the gate was shut, was never recorded, and W is
//  held forever. The player walks into a wall for the rest of the session and
//  nothing in the game can correct it. InputMap does not have this bug
//  because InputMap::Update is called unconditionally from BeginFrame; this
//  is the same discipline arrived at the same way.
//
//  Dispatch stays gated because an input event is script code running, and in
//  EDIT mode no script may run - a key pressed over the Scene view must not
//  reach a game that is not playing.
//
//  AND BECAUSE AN EDGE IS A COMPARISON AND NOT A MOMENT, skipping dispatch
//  for a hundred frames costs nothing: the first dispatch after a pause
//  compares the physical set as it is NOW against the last value it reported,
//  and raises exactly the one change that happened in between.
//
//  ==========================================================================
//  ONCE PER FRAME, WHICH IS NOT ONCE PER STEP
//
//  Stages 100 to 700 run once per fixed STEP, and a frame can ask for zero
//  steps or for four. A dispatcher driven from inside the step loop would
//  deliver one press four times on a slow frame and not at all on a fast one.
//  So dispatch happens at the top of Engine::Simulate, outside the loop,
//  which is also the earliest point a script may safely be called: script
//  rebinding and the focus-regained asset reload have both finished by then.
//
//  ==========================================================================
//  IT CANNOT CHANGE WHAT THE POLLING ANSWERS, AND NOT BY BEING CAREFUL
//
//  There is no call to InputMap anywhere below this line. The two systems
//  read the same event list - EventPump hands out const references and has no
//  mutating accessor - and share nothing else: not a binding, not a context,
//  not a byte of state. InputMap.cpp contains no mention of this file.
//
//  So "did turning events on change my movement code" has the answer NO
//  structurally, rather than after an audit.
//
//  ==========================================================================
//  PHASES, AND WHY TWO OF THEM COINCIDE TODAY
//
//  Unity moves an action through Started -> Performed -> Canceled, where the
//  gap between the first two is an INTERACTION: a Hold raises Started on the
//  press and Performed once the key has been down long enough.
//
//  There are no interactions here, so there is no gap, and a press raises
//  Started and Performed in the same breath. The vocabulary is kept anyway -
//  not because today's Started carries information, but because it is the
//  vocabulary the equivalent Unity code uses, and because a Hold added later
//  needs somewhere to put the delay. A two-phase spelling invented now would
//  have to be unpicked then, and every script written against it rewritten.
//
//  ==========================================================================
//  WHAT IS NOT HERE IN THIS VERSION
//
//  MOUSE POSITION. A "Mouse.Position" binding would read what
//  UISystem::UpdateWindowPointer last wrote, and that call sits INSIDE the
//  fixed-step loop, after this one - so it would always be a frame stale, and
//  on a frame asking for zero steps it would not be written at all. A Look
//  action would freeze solid during a pause menu at a time scale of zero,
//  which is the one place it would most be wanted. Mouse BUTTONS are here and
//  work; the cursor waits until the pointer is fed before dispatch rather
//  than after. Pointer::InView() is the answer in the meantime.
//
//  MOUSE DELTA. RawEvent carries an absolute position and no relative one -
//  SDL's xrel/yrel are discarded in EventPump - so there is nothing honest to
//  build it from. Differencing gated positions gives a full-screen jump
//  whenever the cursor re-enters the view.
//
//  INTERACTIONS AND PROCESSORS. Hold, Tap, MultiTap, deadzones, inversion.
//  All extension points rather than dead ends: an interaction lives between
//  Started and Performed, which is why both phases exist already.
// ============================================================================

#include <engine/input/InputActionAsset.h>
#include <engine/math/Vec2.h>
#include <engine/platform/EventPump.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eng {

enum class ActionPhase {
    Started,
    Performed,
    Canceled,
};

const char* ToString(ActionPhase phase);

// ---------------------------------------------------------------------------
//  One thing that happened to one action. Unity's InputAction.CallbackContext,
//  with Unity's lifetime rule: valid DURING the callback and not after.
//
//  `action` is a view of the map's own stored name, which costs no allocation
//  per event but means a subscriber that keeps the view and reads it after a
//  map swap may be reading freed characters. Copy it if you need to keep it.
//  Unity says the same about CallbackContext, for the same reason.
// ---------------------------------------------------------------------------
struct ActionEvent {
    std::string_view action;
    ActionPhase      phase = ActionPhase::Performed;

    // Both readings of the same thing, always filled in, so a handler never
    // has to know which one this action happens to produce. A Button's value
    // is 1 or 0 and its vector is (value, 0).
    float value = 0.0f;
    Vec2  vector{};

    // WHAT KIND OF ACTION RAISED IT. Carried rather than looked up again,
    // because the answer to "does this send a message?" depends on it:
    // Unity sends on performed, plus canceled for a Value action only. So a
    // Button calls OnJump() once on the press, while a Value also delivers
    // the final zero - which is what tells a script that started moving to
    // stop. Unity's context.action.type, used for the same decision.
    ActionType type = ActionType::Button;
};

// ===========================================================================
//  WHICH CONTROLS ARE PHYSICALLY DOWN. There is one keyboard, so there is one
//  of these.
// ===========================================================================
class InputActions {
public:
    // ---- feeding it -----------------------------------------------------
    //
    //  THE SPAN IS THE REAL ONE AND THE PUMP IS A FILTER ONTO IT. Not a test
    //  seam bolted on: a shipped player installs no GuiHooks, so WasConsumed
    //  is false for every event it ever sees, and the span overload is
    //  precisely what a real game does. The editor's overload drops the
    //  events its GUI claimed, which is what stops typing in the Inspector
    //  from also making the player jump.
    //
    //  AND IT IS WHY THIS IS TESTABLE AT ALL. RawEvent is a plain aggregate
    //  in a header that pulls in no SDL, and EventPump::KeyCodeFromName is a
    //  static needing no SDL_Init - so a test builds a frame's events by hand
    //  and gets a real press edge out of the real edge detector.
    //
    //  THAT IS THE SHAPE InputMap::InjectAction WANTED AND MISSED. That one
    //  writes its state BETWEEN frames, and Update's first step has copied it
    //  into "last frame" before the next frame looks - so an injected action
    //  reads Idle, Held, Idle and is never once Pressed. Measured:
    //  inject(true), Update x3, inject(false), Update x2 gives
    //  "Idle Held Held Held Idle Idle". Here there is no outside-the-window,
    //  because the window IS the list being handed in.
    static void Sample(std::span<const RawEvent> events);
    static void Sample(const EventPump& pump);

    // Everything up, as though the keyboard had been let go of.
    //
    // Sample does this itself on a WindowFocusLost, because alt-tabbing while
    // holding a key is otherwise how a key gets stuck: the KeyUp is delivered
    // to whatever window took the focus and never arrives here at all.
    static void ReleaseAll();

    static bool        IsDown(const Control& control);
    static std::size_t DownCount();
};

// ===========================================================================
//  ONE ACTION MAP, AND WHAT IT LAST SAID.
//
//  Per PlayerInput rather than global, which is what makes two players with
//  two different maps representable - and what stops one player's subscribers
//  hearing the other's Fire.
// ===========================================================================
class ActionMapState {
public:
    ActionMapState() = default;

    // ======================================================================
    //  A DESTRUCTOR THAT EXISTS ONLY TO ANSWER ONE QUESTION: DID I DIE?
    //
    //  Deliver below runs SCRIPT CODE, and script code may destroy the
    //  component this state is a member of - `RemoveComponent` and
    //  `DestroyEntityImmediate` are both immediate, so the destructor runs
    //  before the handler returns, while Dispatch is still on the stack
    //  further down. Every statement after that Deliver then touches freed
    //  storage: the very next one in Dispatch is another Deliver, which
    //  copies m_subscribers out of a vector that no longer exists.
    //
    //  So Deliver and the two functions that call it RETURN WHETHER `this`
    //  SURVIVED, and every caller checks before touching a member again. The
    //  answer cannot live in the object - that is the object being asked
    //  about - so it lives in a shared_ptr<bool> the destructor writes false
    //  to, and Deliver copies the handle onto its own stack BEFORE handing
    //  control to a script. Freeing the state then leaves the flag, and the
    //  copy, perfectly alive.
    //
    //  WHY NOT A FILE-STATIC "the state currently delivering", which would
    //  cost no allocation: it gets the nested case wrong. If A is delivering,
    //  a handler makes B deliver, and B's handler destroys A, then the static
    //  names B and nobody records A's death - and A's loop carries on through
    //  freed memory. A per-state flag cannot be confused that way.
    //
    //  THE COST IS ONE ALLOCATION PER PLAYER, once, at construction. There is
    //  one of these per Player Input component, so there are a handful in a
    //  scene; the Callback list above is std::function and allocates too.
    // ======================================================================
    ~ActionMapState();

    // Non-copyable and non-movable, which declaring the destructor above
    // already made true for moves - stated rather than left implied, because
    // a copy would duplicate the liveness flag and a move would leave one
    // state answering for another's death. Nothing in the engine does either:
    // the only instance is a member of a non-copyable component.
    ActionMapState(const ActionMapState&)            = delete;
    ActionMapState& operator=(const ActionMapState&) = delete;

    // ---- which map ------------------------------------------------------
    //
    //  SWITCHING WHILE A KEY IS HELD IS THE HAZARD, and it is handled rather
    //  than hoped about. Unity's rules, matched:
    //
    //    Everything actuated in the OLD map raises Canceled, so a script that
    //    started something on Pause gets told to stop it.
    //
    //    In the NEW map, a VALUE action already being actuated starts
    //    immediately - Unity's initial state check, which is always on for
    //    Value - so swapping to a map that binds W to Move while W is held
    //    gives Move (0,1) at once rather than nothing until you re-press.
    //
    //    A BUTTON action does NOT, because Unity's initial state check is off
    //    for buttons by default and because the alternative is worse: opening
    //    a menu with Escape would instantly Cancel out of it with the same
    //    press. The held control is recorded as already-seen, so the button
    //    fires on the next genuine press.
    //
    //  Null clears the map, cancelling everything, which is what Disable
    //  means.
    //
    //  RETURNS FALSE WHEN THIS STATE WAS DESTROYED while it was cancelling -
    //  see the destructor. A caller that touches its own members afterwards
    //  has to check; one whose last statement this is does not, and the two
    //  in PlayerInputComponent that do check say so where they do it.
    bool             SetMap(const ActionMap* map);
    const ActionMap* Map() const { return m_map; }

    // ---- the frame ------------------------------------------------------
    //
    //  Works out every action's value from the physical set and raises what
    //  changed since last time. Once per frame, and only when scripts run.
    void Dispatch();

    // Forgets what was last reported, raising nothing. For both ends of a
    // play session: a session must not inherit the last one's values, and
    // must not raise a Canceled on the way out for something the player is
    // still holding.
    void Reset();

    // ---- asking instead of being told -----------------------------------
    //
    //  The polling half of THIS system, which is not InputMap and does not
    //  touch it. Unity has both on the same action for the same reason.
    //
    //  THESE READ WHAT WAS LAST DISPATCHED, not the keyboard. That is not a
    //  shortcut, it is the only way the two halves can agree: a button held
    //  from before this map was switched to raises nothing, so it must also
    //  read as not down, or a script that polls would see a press the script
    //  next to it was never told about. Dispatch runs at the top of Simulate,
    //  before any script, so "last dispatched" is always this frame's answer.
    float Value(std::string_view action) const;
    Vec2  Vector(std::string_view action) const;
    bool  IsDown(std::string_view action) const;

    // ---- listening ------------------------------------------------------
    //
    //  ONE COLLECTIVE LIST, not one per action, which is also how Unity's
    //  onActionTriggered works and is not an accident in either place: a
    //  per-action list has to be made, found and torn down for names that are
    //  strings read out of a file, and every subscriber then needs its own
    //  answer for "what if nothing is bound to that name". One list switched
    //  on event.action has neither problem.
    using Callback = std::function<void(const ActionEvent&)>;
    using Token    = std::uint32_t;

    // 0 is never handed out, so a default-constructed Token is safely "not
    // subscribed" and removes nothing.
    Token Subscribe(Callback callback);
    void  Unsubscribe(Token token);
    void  ClearSubscribers();

    std::size_t SubscriberCount() const { return m_subscribers.size(); }

private:
    struct Reading {
        float value = 0.0f;
        Vec2  vector{};

        // HELD FROM BEFORE THIS MAP, so it does not count yet. A Button that
        // was already down when the map was switched to must not fire, and
        // must not read as down either - but it also cannot simply be
        // recorded at its current value, because then it would read as
        // pressed to anything polling. So it is recorded as zero AND as
        // waiting, and the wait ends when the control is let go of.
        bool suppressed = false;
    };

    struct Subscriber {
        Token    token = 0;
        Callback callback;
    };

    // BOTH ANSWER "AND IS `this` STILL ALIVE?", false meaning it is not and
    // that the caller must touch nothing. See the destructor.
    bool           Deliver(const ActionEvent& event) const;
    bool           CancelEverything();
    const Reading* ReadingFor(std::string_view action) const;

    const ActionMap*     m_map = nullptr;
    std::vector<Reading> m_last;        // parallel to m_map->Actions()

    std::vector<Subscriber> m_subscribers;
    Token                   m_nextToken = 1;

    // TRUE UNTIL THE DESTRUCTOR RUNS, held indirectly so that the answer
    // outlives the object being asked about. Never null for a live state;
    // the check for null in Deliver is there only so that a hypothetical
    // allocation failure degrades to the old behaviour rather than to a
    // crash of its own.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

// ---------------------------------------------------------------------------
//  What one action reads RIGHT NOW from the physical set, with no history and
//  no events. Exposed because it is the whole of the evaluation rule and is
//  worth being able to test and to call directly.
// ---------------------------------------------------------------------------
Vec2 EvaluateAction(const InputAction& action);

// ---------------------------------------------------------------------------
//  THE METHOD AN ACTION CALLS. "Jump" -> "OnJump".
//
//  Unity's rule, reproduced: strip everything that is not a letter, a digit
//  or an underscore, upper-case the first character, put "On" in front. So
//  "Move Camera" calls OnMoveCamera and "fire" calls OnFire.
//
//  DONE FORWARDS, FROM THE ACTION, and never backwards from the method. The
//  reverse has no single answer - OnMoveCamera could have come from "Move
//  Camera", "MoveCamera", "move camera" or "move-camera" - so the scanner
//  records method names and this is applied to the action at dispatch, which
//  is exactly the direction Unity goes.
//
//  WHERE THIS DIVERGES, DELIBERATELY: Unity lets two actions quietly mangle
//  to one method and says nothing, and throws outright on a name with no
//  usable characters at all. This produces the same names for every ordinary
//  case, and the collisions are REPORTED by whoever loads the map rather than
//  silently merged. An empty result means "no method can be named after this
//  action", which the caller says out loud instead of guessing.
// ---------------------------------------------------------------------------
std::string ActionHandlerMethodName(std::string_view action);

} // namespace eng

// ===========================================================================
//  ENGINE_CALL_ACTION_HANDLER - written only by generated code.
//
//  Calls one handler with whichever of the three shapes it actually declared:
//
//      void OnMove(eng::Vec2 direction)   // the whole value
//      void OnLean(float amount)          // one number
//      void OnJump()                      // just that it happened
//
//  THE LADDER IS WHY THE SCANNER IS ALLOWED TO GUESS. Every arm is an
//  `if constexpr (requires ...)`, so a method the scanner picked up by
//  mistake - a private one, one taking an eng::EntityId, one that is not a
//  member at all - matches no arm and the call COMPILES AWAY. Nothing a text
//  scan believes can turn a working script into a build failure, which is the
//  rule tools/ScriptVars.h states and the thing an earlier design of this
//  broke.
//
//  IT ALSO MEANS THE SCANNER NEVER HAS TO READ A PARAMETER LIST. It records
//  names; C++ picks the overload. One less thing for a whitelist to be wrong
//  about.
//
//  THE ORDER IS MOST-SPECIFIC-FIRST and the three are mutually exclusive
//  rather than merely ordered: eng::Vec2 has exactly two constructors and
//  neither takes one float, so a handler wanting a float cannot be fed a
//  vector and a handler wanting a vector cannot be fed a float.
//
//  USED INSIDE A TEMPLATE, always - see what GenerateScriptUnit emits. A
//  requires-expression in a dependent context is unambiguously a substitution
//  check; outside one it is a question compilers have been known to answer by
//  erroring rather than by saying no.
// ===========================================================================
// ===========================================================================
//  DOES THIS METHOD ACTUALLY BIND? The same three shapes as the ladder below,
//  asked as a constant rather than acted on.
//
//  It exists so that the engine can tell a candidate the scanner FOUND from a
//  handler that will really be called. `void OnHit(eng::EntityId)` and a
//  private `void OnSecret()` both look like handlers in the source and
//  neither can ever fire - listing them at load time as though they could
//  would send somebody hunting for a binding that was never the problem.
//
//  AN IMMEDIATELY-CALLED GENERIC LAMBDA, so the requires-expressions sit in a
//  dependent context - the same reason the generated dispatcher is a template.
// ===========================================================================
#define ENGINE_ACTION_HANDLER_BINDS(Type, Method)                              \
    ([]<class T>() constexpr {                                                 \
        return requires(T& s, const ::eng::ActionEvent& e) {                   \
            s.Method(e.vector);                                                \
        } || requires(T& s, const ::eng::ActionEvent& e) {                     \
            s.Method(e.value);                                                 \
        } || requires(T& s) { s.Method(); };                                   \
    }.template operator()<Type>())

#define ENGINE_CALL_ACTION_HANDLER(self, Method, event)                        \
    do {                                                                       \
        if constexpr (requires { (self).Method((event).vector); }) {            \
            (self).Method((event).vector);                                      \
        } else if constexpr (requires { (self).Method((event).value); }) {      \
            (self).Method((event).value);                                       \
        } else if constexpr (requires { (self).Method(); }) {                   \
            (self).Method();                                                    \
        }                                                                       \
    } while (false)
