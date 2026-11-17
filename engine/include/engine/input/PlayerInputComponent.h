#pragma once

// ============================================================================
//  PlayerInputComponent.h - Unity's PlayerInput, as a component.
//
//  Put one on the entity that is the player, point it at a .oinput file, and
//  name which of that file's action maps starts current:
//
//      { "type": "PlayerInputComponent",
//        "actions": "input/player.oinput",
//        "defaultMap": "Gameplay" }
//
//  From another script, hold a reference and change what the controls mean:
//
//      class PauseMenu final : public MyScriptBehaviour {
//      public:
//          PlayerInputComponent player;      // drag the Player here
//
//          void Open()  { player->SwitchCurrentActionMap("Menu"); }
//          void Close() { player->SwitchCurrentActionMap("Gameplay"); }
//      };
//
//  THAT IS WHY THIS IS A COMPONENT AND NOT A GLOBAL. The maps and the
//  evaluation would work perfectly well as one static thing; what would not
//  work is a second player, an Inspector row, a scene file that remembers the
//  choice, or one script reaching another entity's controls. Unity reached
//  the same conclusion, and its automatic OnJump() dispatch is a property of
//  this component rather than of actions - without it there is no auto-call
//  at all, only the subscription below.
//
//  ==========================================================================
//  THE AUTHORED MAP AND THE CURRENT MAP ARE TWO DIFFERENT MEMBERS
//
//  "defaultMap" is saved. The current map is not, and SwitchCurrentActionMap
//  writes ONLY the current one.
//
//  This is not tidiness - it is the trap Component.h spells out for
//  SpriteComponent, and it bites exactly the same way here. Ctrl+S is not
//  blocked while the game is playing, and every undo snapshot is a whole
//  SaveToString; so a run-time write to a saved field eventually becomes the
//  authored value, and the next time the scene is opened the player starts in
//  the pause menu. SetTexture and ShowTexture are two calls for this reason,
//  and so are these two members.
//
//  ==========================================================================
//  THE ASSET IS RESOLVED LAZILY, ON THE FIRST ASK
//
//  Not in Deserialize and not in OnAttach. Components attach in the order the
//  file lists them, and OnAttach runs while the rest of the entity may still
//  be half built - SpriteAnimation.cpp makes the same choice for the same
//  reason. A missing file is remembered as missing so a broken path does not
//  re-read the disk every frame; SetActions and ReloadActions clear that, and
//  the editor calls the second one when the file changes underneath.
//
//  ==========================================================================
//  ONE PER ENTITY. AllowsSeveral stays false, which is Unity's rule too, and
//  the Inspector hides the "add another" button accordingly. Two players are
//  two entities.
// ============================================================================

#include <engine/input/InputActionAsset.h>
#include <engine/input/InputActions.h>
#include <engine/math/Vec2.h>
#include <engine/scene/Component.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace eng {

// Forward-declared rather than included: the per-action Subscribe below needs
// to know whether an owner IS a script, and nothing else here needs the class.
// The test is made where Subscribe is INSTANTIATED, which for a script is its
// own unit - where the class is necessarily complete, since that is where the
// script derives from it. See OwnerKey.
class ScriptBehaviour;

// ---------------------------------------------------------------------------
//  HOW HANDLERS GET CALLED. Unity's Behavior dropdown, with three of its four
//  options - and the fourth left out rather than stubbed.
//
//  Unity's "Invoke Unity Events" is a per-action UnityEvent wired up by
//  picking a method from a dropdown in the Inspector. The whole of what it is
//  is that picking, and reproducing it needs run-time reflection over method
//  names, which this engine has nowhere to get. A fourth value that silently
//  did nothing would be worse than three that work, so it is absent and this
//  comment is why.
// ---------------------------------------------------------------------------
enum class InputNotification {
    // Scripts on THIS entity. Unity's default, and this one's.
    SendMessages,

    // This entity and everything under it. Unity's BroadcastMessage includes
    // the object itself, which "broadcasts down the hierarchy" does not
    // obviously say - so it is spelled out here and matched.
    BroadcastMessages,

    // No automatic calls at all: only whoever asked for them with Subscribe.
    // Unity's "Invoke C# Events", whose onActionTriggered is one collective
    // callback for every action - which is what Subscribe already is.
    Callbacks,
};

const char* ToString(InputNotification notification);

// ---------------------------------------------------------------------------
//  WHICH EVENTS SEND A MESSAGE, which is not all of them.
//
//  Unity's rule exactly: Performed, plus Canceled for anything that is not a
//  Button. So a Button calls OnJump() ONCE, on the press - there is no second
//  call on the release, and a script that wants one polls IsDown. A Value
//  calls OnMove(direction) on every change AND once more with zero when it
//  stops, so a script that started moving on the first is told to stop by the
//  last.
//
//  STARTED NEVER SENDS. It arrives in the same breath as Performed, because
//  this engine has no interactions to separate the two, so sending on both
//  would call every handler twice - which is exactly what this did before the
//  rule was written down. Subscribers still see Started; they asked for
//  everything.
//
//  ==========================================================================
//  EXPOSED SO THAT THE RULE CAN BE TESTED RATHER THAN TRANSCRIBED, which is
//  the same reason eng::EvaluateAction is exposed one header over. It was
//  previously three lines inside the private Route, and the test for it was a
//  HAND COPY of those three lines living in the test file - so the shipped
//  rule could have been inverted or deleted with every case still green. A
//  rule worth a test is a rule the test has to be able to call.
//  ==========================================================================
bool ActionSendsMessage(const ActionEvent& event);

class PlayerInputComponent final : public Component {
    // Only so that ForgetSubscriber can reach RemoveBindings. Nothing else
    // outside this class has any business naming a subscription by its key.
    friend class PlayerInputSystem;

public:
    static constexpr const char* kTypeName = "PlayerInputComponent";

    PlayerInputComponent() = default;
    ~PlayerInputComponent() override;

    // NOT COPYABLE. The system keeps raw pointers to these, and a moved-from
    // component would leave two addresses where it knows one - the same
    // reason SpriteAnimationComponent deletes its copy.
    PlayerInputComponent(const PlayerInputComponent&)            = delete;
    PlayerInputComponent& operator=(const PlayerInputComponent&) = delete;

    const char* TypeName() const override { return kTypeName; }

    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;

    void OnAttach() override;
    void OnDetach() override;

    // ---- the asset -------------------------------------------------------
    const std::string& ActionsPath() const { return m_actionsPath; }

    // Points at a different .oinput and starts on that file's default map.
    // Everything the old map had running is cancelled first.
    bool SetActions(std::string_view virtualPath, std::string& outError);

    // Forgets a failed lookup so the next ask tries the disk again. For the
    // editor, after the file has been edited or reloaded.
    void ReloadActions();

    const InputActionAsset* Asset() const;

    // ---- maps ------------------------------------------------------------
    //
    //  DefaultMapName is the SAVED one and CurrentActionMapName is the LIVE
    //  one. See the note above for why conflating them loses people's scenes.
    const std::string& DefaultMapName() const { return m_defaultMap; }
    std::string        CurrentActionMapName() const;

    // THE AUTHORED ONE, for the Inspector. This is the only thing allowed to
    // write the field Serialize reads, which is why it is spelled nothing
    // like SwitchCurrentActionMap - the two must never be reachable by
    // accident from each other.
    //
    // It moves the current map too. An Inspector field that visibly does
    // nothing until the next time you press Play is worse than one that acts
    // at once, and in edit mode the two are the same thing anyway.
    void SetDefaultMapName(std::string_view name);

    // Unity's spelling. False when this asset has no map by that name, having
    // left the current map alone and said so in the log.
    bool SwitchCurrentActionMap(std::string_view name);

    // ---- on and off ------------------------------------------------------
    //
    //  Disable cancels everything in flight, so a script that started
    //  something on a press is told to stop it rather than being left
    //  holding it. Enable puts the same map back.
    void Enable();
    void Disable();
    bool IsEnabled() const { return m_enabled; }

    // ---- how handlers are called ---------------------------------------
    InputNotification Notifications() const { return m_notifications; }
    void              SetNotifications(InputNotification notifications);

    // ---- asking -----------------------------------------------------------
    float Value(std::string_view action) const;
    Vec2  Vector(std::string_view action) const;
    bool  IsDown(std::string_view action) const;

    // ---- being told --------------------------------------------------------
    //
    //  Unity's onActionTriggered: one collective subscription, every action,
    //  every phase. The generated OnJump() dispatch is built on top of this.
    using Callback = ActionMapState::Callback;
    using Token    = ActionMapState::Token;

    Token Subscribe(Callback callback) { return m_state.Subscribe(std::move(callback)); }
    void  Unsubscribe(Token token) { m_state.Unsubscribe(token); }
    std::size_t SubscriberCount() const { return m_state.SubscriberCount(); }

    // =======================================================================
    //  ONE ACTION, ONE METHOD - and no lambda, no token and no vector of
    //  tokens for the script to keep.
    //
    //      void OnStart() {
    //          input->Subscribe("Fire", this, &Gun::FireGun);   // void FireGun()
    //          input->Subscribe("Move", this, &Gun::Aim);       // void Aim(Vec2)
    //      }
    //
    //  THE SHAPE OF THE METHOD IS THE SAME QUESTION THE OnJump() DISPATCH
    //  ASKS, answered by the same ladder: a method taking eng::Vec2 is handed
    //  the vector, one taking float is handed the value, one taking
    //  const ActionEvent& is handed the whole event, and one taking nothing
    //  is simply called. The three value shapes are mutually exclusive -
    //  Vec2 has no one-float constructor - so there is nothing to
    //  disambiguate. A method matching none of them is a compile error here
    //  rather than a handler that silently never fires, which is the one
    //  thing the On-prefixed spelling cannot offer.
    //
    //  IT IS CALLED WHEN A HANDLER WOULD BE CALLED - ActionSendsMessage,
    //  exactly as for OnFire(). So one press is ONE call, and a Value action
    //  also gets the final zero. The collective Subscribe above is still the
    //  way to see every phase; this is the way to see the ones that mean
    //  something.
    //
    //  AND IT WORKS IN EVERY NOTIFICATION MODE. It is not a message, so the
    //  Behaviour dropdown does not switch it off; Callbacks silences the
    //  automatic OnFire() calls and leaves these running, which is what makes
    //  that mode usable from one script without disarming the others.
    //
    //  ==================== WHY THERE IS NO TOKEN HERE ====================
    //
    //  The subscription is remembered against the OWNER, and the owner is
    //  the object whose method is to be called - so the component already
    //  knows the one thing a token was being kept to say. Unsubscribe(this)
    //  takes back everything that object asked for, in one call, with
    //  nothing stored on its side.
    //
    //  AND A SCRIPT DOES NOT HAVE TO MAKE THAT CALL EITHER. ~ScriptBehaviour
    //  tells every Player Input component that the object is gone, so a
    //  rebuilt script, a removed Script Component and a destroyed entity all
    //  leave no binding behind. That is not politeness: SetScriptName
    //  destroys the behaviour and builds a new one WHILE THIS COMPONENT
    //  LIVES ON, so a remembered `this` would be read again on the next
    //  press. Unsubscribe(this) remains for a script that wants to stop
    //  listening while it is still alive.
    //
    //  WHAT IS STORED IS THE ACTION'S NAME, A COPY OF IT. Not an
    //  InputAction*, which InputActionAsset.h says is not yours to keep:
    //  Reload re-fills the map vector, so a pointer taken before the Input
    //  Actions window saved dangles after it. A name survives the save, the
    //  map switch, and the file being pointed somewhere else entirely.
    // =======================================================================
    template <class Obj, class Method>
    void Subscribe(std::string_view action, Obj* owner, Method method) {
        static_assert(std::is_member_function_pointer_v<Method>,
                      "The third argument must be a pointer to a member function, "
                      "such as &MyScript::FireGun.");
        // ASKED OF THE TYPES AND NOT OF THE VARIABLES, which is why each
        // requires-expression declares its own. `owner` and `method` are
        // function parameters, and naming one in a constant expression is a
        // question compilers answer inconsistently even where it is
        // unevaluated. ENGINE_ACTION_HANDLER_BINDS is written the same way
        // for the same reason.
        constexpr bool kVector =
            requires(Obj& o, Method m, const ActionEvent& e) { (o.*m)(e.vector); };
        constexpr bool kValue =
            requires(Obj& o, Method m, const ActionEvent& e) { (o.*m)(e.value); };
        constexpr bool kEvent =
            requires(Obj& o, Method m, const ActionEvent& e) { (o.*m)(e); };
        constexpr bool kNothing = requires(Obj& o, Method m) { (o.*m)(); };

        static_assert(kVector || kValue || kEvent || kNothing,
                      "A subscribed method must take eng::Vec2, float, "
                      "const eng::ActionEvent&, or nothing at all - and must "
                      "return void.");

        if (owner == nullptr || method == nullptr) {
            return;
        }
        AddBinding(action, OwnerKey(owner),
                   [owner, method](const ActionEvent& event) {
                       // A method that takes nothing uses none of it, and /W4 says so.
                       (void)event;
                       if constexpr (kVector) {
                           (owner->*method)(event.vector);
                       } else if constexpr (kValue) {
                           (owner->*method)(event.value);
                       } else if constexpr (kEvent) {
                           (owner->*method)(event);
                       } else {
                           (owner->*method)();
                       }
                   });
    }

    // Everything that object subscribed to, in one call. Safe from inside a
    // callback, and safe to call twice. A script does not need it - see
    // above - but one that stops listening early does.
    //
    // A TEMPLATE RATHER THAN void*, so that Unsubscribe(this) cannot
    // silently mean a number: deduction only succeeds for an actual pointer,
    // and Unsubscribe(nullptr) is a compile error rather than a guess.
    //
    // Unsubscribe(0) STILL MEANS THE TOKEN OVERLOAD, and is left meaning it.
    // Literal 0 is an int, deduction against Obj* fails, so this template is
    // not a candidate at all - and Token 0 is defined as "not subscribed",
    // so that call is a documented no-op rather than something to diagnose.
    template <class Obj>
    void Unsubscribe(Obj* owner) { RemoveBindings(OwnerKey(owner)); }

    // How many per-action subscriptions are live. For tests and for the
    // Inspector; a script has no reason to ask.
    std::size_t BindingCount() const;

    // ---- driven by PlayerInputSystem ----------------------------------------
    void Dispatch();
    void ResetState();

private:
    void EnsureResolved();
    void Route(const ActionEvent& event);

    // =======================================================================
    //  THE KEY A SUBSCRIPTION IS REMEMBERED UNDER, which for a script is its
    //  ScriptBehaviour subobject and not its own `this`.
    //
    //  THE TWO ADDRESSES ARE USUALLY THE SAME AND THE CODE MUST NOT RELY ON
    //  IT. ~ScriptBehaviour is what reports the death, and all it has is the
    //  base pointer; a script subscribes with the derived one. Converting
    //  here - where the derived class is complete, because this is being
    //  instantiated inside the script that wrote the call - makes the two
    //  agree by construction rather than by layout.
    //
    //  THE TARGET IS `const ScriptBehaviour*` AND THE CONST IS LIVE CODE.
    //  Obj is deduced from the argument, so `this` inside a CONST method
    //  deduces `const Gun` - and is_convertible_v<const Gun*,
    //  ScriptBehaviour*> is FALSE, because that conversion would drop const.
    //  Written that way the else arm is taken, the derived address is stored,
    //  and the normalisation above quietly stops happening for exactly the
    //  scripts that most need it. It costs nothing where the two addresses
    //  coincide, which is why nothing notices until a script has a second
    //  base - and then the destructor's key never matches and the binding
    //  outlives the object.
    //
    //  An owner that is not a script keeps its own address, which is right:
    //  nothing will be reporting its death, and it is expected to call
    //  Unsubscribe itself.
    // =======================================================================
    template <class Obj>
    static const void* OwnerKey(Obj* owner) {
        if constexpr (std::is_convertible_v<Obj*, const ScriptBehaviour*>) {
            return static_cast<const ScriptBehaviour*>(owner);
        } else {
            return owner;
        }
    }

    // One per-action subscription. `owner` is a KEY AND NEVER DEREFERENCED -
    // the callback holds its own typed copy - so a stale one can only fail to
    // match, never crash.
    struct Binding {
        std::string action;             // a copy; see the header note on Reload
        const void* owner = nullptr;    // null once dropped, until compaction
        Callback    call;
    };

    void AddBinding(std::string_view action, const void* owner, Callback call);
    void RemoveBindings(const void* owner);
    void CompactBindings();

    // Answers false when a callback destroyed this component, exactly as
    // ActionMapState::Deliver does and for the same reason.
    bool DeliverBindings(const ActionEvent& event);

    // SAVED, and written by Serialize.
    std::string m_actionsPath;
    std::string m_defaultMap;
    InputNotification m_notifications = InputNotification::SendMessages;

    // LIVE, and written by nothing that Serialize reads.
    const InputActionAsset* m_asset    = nullptr;
    bool                    m_resolved = false;   // tried, whatever the answer
    std::string             m_currentMap;
    ActionMapState          m_state;
    bool                    m_enabled = true;

    // The routing subscription, taken out once and kept - so that neither a
    // map switch nor a second Deserialize leaves two of them delivering
    // every event twice.
    ActionMapState::Token m_routing = 0;

    // =======================================================================
    //  THE PER-ACTION SUBSCRIPTIONS.
    //
    //  HELD BY POINTER SO THE ADDRESSES ARE STABLE. A callback may subscribe,
    //  which grows the vector and would move every Binding out from under the
    //  walk in DeliverBindings. One indirection buys an allocation per
    //  subscription - a handful, at OnStart - and no snapshot copy per event.
    //
    //  AND REMOVAL IS DEFERRED, never immediate: Unsubscribe nulls the owner
    //  and compaction happens when nothing is walking. A callback that
    //  unsubscribes itself, or its neighbour, therefore cannot destroy the
    //  std::function it is currently running inside.
    // =======================================================================
    std::vector<std::unique_ptr<Binding>> m_bindings;
    int                                   m_bindingDepth = 0;

    // TRUE UNTIL THE DESTRUCTOR RUNS, held indirectly so that the answer
    // outlives the object being asked about. The same device ActionMapState
    // uses, for the same reason: a subscribed method is script code, and
    // script code may destroy this component while DeliverBindings is still
    // walking further down the stack.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

// ===========================================================================
//  PlayerInputSystem - every PlayerInputComponent, dispatched once a frame.
//
//  A PLAIN STATIC, NOT A SCHEDULED System STAGE, and that is deliberate.
//  Stages at or above kFirstRenderStage are driven from Engine::RenderWorld,
//  which the EDITOR CALLS TWICE A FRAME - once for the Game view and once for
//  the Scene view - so a render-stage system would dispatch twice in the
//  editor and once in a shipped game. ScriptComponent.h calls that out at
//  length; this is the same hazard.
//
//  And it cannot go in the fixed-step range either: stages 100 to 700 run
//  once per STEP, and a frame may ask for four steps or none, so one press
//  would arrive four times on a slow frame and never on a fast one.
//
//  So it is called by hand from the top of Engine::Simulate, which is once
//  per frame AND before the fixed-step loop - the second half mattering
//  because a script polling Value() from OnFixedUpdate must read THIS
//  frame's dispatch rather than the previous one's.
// ===========================================================================
class PlayerInputSystem {
public:
    static void RegisterComponentTypes();

    static void Register(PlayerInputComponent& component);
    static void Unregister(PlayerInputComponent& component);

    // =======================================================================
    //  THAT OBJECT IS GONE: EVERY COMPONENT FORGETS WHAT IT SUBSCRIBED.
    //
    //  CALLED BY ~ScriptBehaviour, and that is what makes the per-action
    //  Subscribe safe to use without writing an OnDestroy. ScriptComponent
    //  destroys and rebuilds its behaviour on a script rebuild, on a rename
    //  and on "Remove Component" WHILE THE PLAYER INPUT COMPONENT LIVES ON,
    //  so a remembered `this` outlives the object it names and would be
    //  called again on the next press.
    //
    //  THE POINTER IS A KEY AND IS NEVER DEREFERENCED here or anywhere else,
    //  which is what makes it safe to pass one that is already half
    //  destroyed - a base destructor is exactly that.
    // =======================================================================
    static void ForgetSubscriber(const void* owner);

    // Once per frame, before the fixed-step loop.
    static void FrameDispatch();

    // Every live component forgets what it last reported, raising nothing.
    // Called at both ends of a play session: a session must not inherit the
    // previous one's values, and must not raise a Canceled on the way out for
    // something the player is still holding.
    static void ResetAll();

    // =======================================================================
    //  EVERY COMPONENT READING THIS FILE RESOLVES AGAIN.
    //
    //  CALLED BY THE EDITOR AFTER THE INPUT ACTIONS WINDOW SAVES, and that
    //  is not optional. InputActionLibrary::Reload re-fills the live asset's
    //  map vector IN PLACE, so every ActionMap* handed out before the save
    //  dies with the old contents - and a component caches exactly one of
    //  those, resolved once and never re-read. Without this call the loop
    //  the window exists for (edit a binding, Save, press Play) drives a
    //  freed or a renumbered map while CurrentActionMapName() still reports
    //  the right name.
    //
    //  MATCHED TWO WAYS. A component currently HOLDING the asset is the
    //  certain case, found by identity so a differently spelled path cannot
    //  miss it. A component that merely NAMES the path is the retry case:
    //  its load failed, it has no asset to compare, and a save is exactly
    //  the event that may have fixed the file.
    //
    //  OTHER FILES ARE LEFT ALONE deliberately: re-resolving cancels what a
    //  player has in flight, and a player bound to a file nobody touched
    //  should not feel somebody else's save.
    // =======================================================================
    static void ReloadAll(std::string_view virtualPath);

    static void        Clear();
    static std::size_t Count();
};

} // namespace eng
