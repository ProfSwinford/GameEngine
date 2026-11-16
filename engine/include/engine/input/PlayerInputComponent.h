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
#include <string>
#include <string_view>

namespace eng {

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

    // ---- driven by PlayerInputSystem ----------------------------------------
    void Dispatch();
    void ResetState();

private:
    void EnsureResolved();
    void Route(const ActionEvent& event);

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
