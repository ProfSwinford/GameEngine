#include <engine/input/PlayerInputComponent.h>

#include <engine/core/Log.h>
#include <engine/scene/Entity.h>
#include <engine/scene/Scene.h>
#include <engine/scene/ScriptComponent.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace eng {

const char* ToString(InputNotification notification) {
    switch (notification) {
        case InputNotification::SendMessages:      return "SendMessages";
        case InputNotification::BroadcastMessages: return "BroadcastMessages";
        case InputNotification::Callbacks:         return "Callbacks";
    }
    return "?";
}

namespace {

InputNotification NotificationFromName(std::string_view text, bool& outKnown) {
    outKnown = true;
    if (text == "SendMessages")      { return InputNotification::SendMessages; }
    if (text == "BroadcastMessages") { return InputNotification::BroadcastMessages; }
    if (text == "Callbacks")         { return InputNotification::Callbacks; }
    outKnown = false;
    return InputNotification::SendMessages;
}

// Every entity this component's messages reach, as IDS and not as pointers.
//
// WHY IDS. The list is taken before the first handler runs, and a handler is
// script code - it may destroy its own entity, or a sibling, or the whole
// subtree. Held as pointers, the rest of the walk would then be reading freed
// components; held as ids, a destroyed entity simply stops resolving and is
// skipped. Scene::Get answers null for a stale id rather than a wrong entity,
// because destroying one bumps its slot's generation.
void CollectTargets(Entity& root, bool includeChildren, std::vector<EntityId>& into) {
    into.push_back(root.Id());
    if (!includeChildren) {
        return;
    }
    for (std::size_t i = 0; i < root.ChildCount(); ++i) {
        if (Entity* child = root.ChildAt(i)) {
            CollectTargets(*child, true, into);
        }
    }
}

} // namespace

namespace {

// Every PlayerInputComponent currently attached to something. The system
// walks this rather than every entity in the scene asking "are you a player?".
std::vector<PlayerInputComponent*> g_players;

// ---------------------------------------------------------------------------
//  IS A WALK OVER g_players IN PROGRESS?
//
//  Asked by Unregister, and the answer decides whether a dying component is
//  ERASED from the list or merely written over with nullptr.
//
//  All three walks below hold a cursor INTO this vector while running script
//  code, and an erase at or before that cursor pulls a LATER entry past it -
//  so the player after the one that died is silently never dispatched. With
//  three players, the cursor on the second, and its handler destroying the
//  first: the list becomes [P1, P2], size 2, ++i reaches 2, and P2 is not
//  dispatched at all.
//
//  AND IT IS NOT MERELY A FRAME LATE. Dispatch is edge-based against m_last,
//  so an action whose press and release both fall inside the skipped frame
//  raises nothing, ever - the next frame compares two equal readings and sees
//  no change to report.
//
//  A COUNT AND NOT A BOOLEAN, so that a walk reached from inside another one
//  cannot clear the flag on its way out and leave the outer cursor unguarded.
//
//  THIS IS ScriptSystem'S PATTERN, taken deliberately rather than invented
//  again: g_walkDepth at ScriptComponent.cpp:53, the conditional tombstone at
//  :1677, the erase_if sweep at :1822, Count() as a count_if at :1703. One
//  thing differs. Those walks already ended in an erase_if of their own, so
//  the sweep came free; these three do not, so the sweep lives in the guard's
//  destructor below rather than at the end of each walk.
// ---------------------------------------------------------------------------
int g_walkDepth = 0;

// Counts a walk in for as long as it is in scope, so that an early return out
// of one cannot leave the count standing.
struct WalkScope {
    WalkScope() { ++g_walkDepth; }

    ~WalkScope() {
        // THE SWEEP IS HERE, for two reasons. It cannot be forgotten by a
        // fourth walk added later - declaring the guard is the whole of what
        // such a walk has to remember - and only the OUTERMOST walk sweeps,
        // because an inner one's sweep would move entries under the outer
        // one's cursor, which is precisely the bug all of this exists for.
        if (--g_walkDepth == 0) {
            std::erase_if(g_players, [](const PlayerInputComponent* player) {
                return player == nullptr;
            });
        }
    }

    WalkScope(const WalkScope&)            = delete;
    WalkScope& operator=(const WalkScope&) = delete;
};

} // namespace

PlayerInputComponent::~PlayerInputComponent() {
    // A safety net. OnDetach is where the removal normally happens; this
    // catches a component that was built but never attached, which is what
    // happens when loading a scene fails partway through - the scene loader
    // creates and deserializes before it attaches.
    PlayerInputSystem::Unregister(*this);
}

// ---------------------------------------------------------------------------

bool PlayerInputComponent::Deserialize(const Json& node, std::string& outError) {
    // AN EMPTY PATH IS SUCCESS, not a refusal. That is exactly the state the
    // Inspector's "Add Component" button produces, and a component that
    // refused it would be unusable from the moment it was added.
    m_actionsPath = ReadString(node, "actions", "", kTypeName);
    m_defaultMap  = ReadString(node, "defaultMap", "", kTypeName);
    m_enabled     = ReadBool(node, "enabled", true, kTypeName);

    // Unity's Behavior dropdown. An unknown value falls back to the
    // default and says which values exist, rather than refusing a scene
    // over a spelling.
    const std::string behaviour =
        ReadString(node, "behaviour", "SendMessages", kTypeName);
    bool known       = false;
    m_notifications  = NotificationFromName(behaviour, known);
    if (!known) {
        outError = "PlayerInputComponent.behaviour is '" + behaviour +
                   "'; expected SendMessages, BroadcastMessages or Callbacks";
    }

    // Nothing is loaded here. Components attach in the order the file lists
    // them, and resolving an asset mid-load is how a component ends up
    // reading a sibling that does not exist yet.
    m_asset    = nullptr;
    m_resolved = false;
    m_currentMap.clear();
    m_state.SetMap(nullptr);

    return outError.empty();
}

bool PlayerInputComponent::Serialize(Json& out) const {
    // "type" is NOT written here - the scene writes it, so that no component
    // can get its own name wrong.
    out["actions"] = m_actionsPath;

    // ONLY THE AUTHORED MAP. m_currentMap is deliberately not saved: see the
    // header. A map switched to at run time must not become what the scene
    // opens on next time.
    if (!m_defaultMap.empty()) {
        out["defaultMap"] = m_defaultMap;
    }
    // Written only when it differs from the default, so an ordinary component
    // does not grow a key and every scene file is not rewritten.
    if (!m_enabled) {
        out["enabled"] = false;
    }
    if (m_notifications != InputNotification::SendMessages) {
        out["behaviour"] = ToString(m_notifications);
    }
    return true;
}

// The whole of the rule. See the header for which parts of it are Unity's and
// why Started is not one of them.
bool ActionSendsMessage(const ActionEvent& event) {
    return event.phase == ActionPhase::Performed ||
           (event.phase == ActionPhase::Canceled && event.type != ActionType::Button);
}

void PlayerInputComponent::OnAttach() { PlayerInputSystem::Register(*this); }

void PlayerInputComponent::OnDetach() {
    // Everything in flight is cancelled on the way out, so a script that
    // started something on a press hears the end of it rather than being left
    // holding it forever.
    //
    // AND ONE OF THOSE CANCELS CAN TAKE US WITH IT - a handler that removes
    // this very component, which is immediate. There is nothing to clean up
    // in that case: the destructor unregisters as its own safety net, so by
    // the time SetMap answers false it has already happened. Carrying on to
    // call Unregister would mean taking the address of a destroyed object.
    if (!m_state.SetMap(nullptr)) {
        return;
    }
    PlayerInputSystem::Unregister(*this);
}

// ---------------------------------------------------------------------------

void PlayerInputComponent::Route(const ActionEvent& event) {
    // "Invoke C# Events" in Unity means the events and NOTHING ELSE - no
    // messages are sent. Subscribers have already been told by the time this
    // runs, so there is simply nothing left to do.
    if (m_notifications == InputNotification::Callbacks) {
        return;
    }

    // Not every event is a message. The rule, and why it is a named function
    // rather than three lines here, are on ActionSendsMessage in the header.
    if (!ActionSendsMessage(event)) {
        return;
    }

    Entity* owner = Owner();
    Scene*  scene = GetScene();
    if (owner == nullptr || scene == nullptr) {
        return;
    }

    // "Jump" -> "OnJump", by Unity's rule, once per event rather than once
    // per script.
    const std::string method = ActionHandlerMethodName(event.action);
    if (method.empty()) {
        return;
    }

    std::vector<EntityId> targets;
    CollectTargets(*owner, m_notifications == InputNotification::BroadcastMessages,
                   targets);

    std::vector<ScriptComponent*> scripts;
    for (const EntityId id : targets) {
        Entity* entity = scene->Get(id);
        if (entity == nullptr) {
            continue;   // a handler destroyed it since the list was taken
        }

        scripts.clear();
        entity->FindAll<ScriptComponent>(scripts);
        for (ScriptComponent* script : scripts) {
            if (script == nullptr) {
                continue;
            }
            ScriptBehaviour* behaviour = script->Behaviour();
            if (behaviour == nullptr) {
                continue;   // the script is attached but not compiled in
            }

            const ScriptRegistry::Entry* entry =
                ScriptRegistry::Find(script->ScriptName());
            if (entry == nullptr || entry->actionDispatch == nullptr) {
                continue;   // that script declares no handlers at all
            }

            for (std::size_t i = 0; i < entry->actionHandlerCount; ++i) {
                // A candidate the compiler could not reach - private, or
                // taking something no action can supply. The generated call
                // for it compiles to nothing, so this only saves the work.
                if (entry->actionHandlerBound != nullptr &&
                    !entry->actionHandlerBound[i]) {
                    continue;
                }
                if (method == entry->actionHandlers[i]) {
                    entry->actionDispatch(*behaviour, i, event);
                    break;
                }
            }
        }
    }
}

void PlayerInputComponent::SetNotifications(InputNotification notifications) {
    m_notifications = notifications;
}

void PlayerInputComponent::EnsureResolved() {
    // TAKEN OUT ONCE AND KEPT. Not in OnAttach, because a component that is
    // deserialized and never attached would then never route; not per
    // SetMap, because switching maps does not clear subscribers and two
    // routings would deliver every event twice.
    if (m_routing == 0) {
        m_routing = m_state.Subscribe(
            [this](const ActionEvent& event) { Route(event); });
    }

    if (m_resolved) {
        return;
    }
    m_resolved = true;

    if (m_actionsPath.empty()) {
        return;
    }

    std::string error;
    m_asset = InputActionLibrary::Load(m_actionsPath, error);
    if (m_asset == nullptr) {
        // SAID ONCE, not every frame. m_resolved is already true, so a broken
        // path costs one message and one disk read rather than sixty a
        // second - and ReloadActions is how the editor says "try again".
        ENGINE_LOG_ERROR(Channels::kInput, "PlayerInput on '{}': {}",
                         Owner() != nullptr ? Owner()->Name() : "<unattached>", error);
        return;
    }

    const ActionMap* map = m_defaultMap.empty() ? m_asset->DefaultMap()
                                                : m_asset->FindMap(m_defaultMap);
    if (map == nullptr) {
        ENGINE_LOG_WARN(Channels::kInput,
                        "PlayerInput on '{}': '{}' has no action map called '{}'; "
                        "using '{}' instead",
                        Owner() != nullptr ? Owner()->Name() : "<unattached>",
                        m_actionsPath, m_defaultMap,
                        m_asset->DefaultMap() != nullptr ? m_asset->DefaultMap()->Name()
                                                         : std::string{});
        map = m_asset->DefaultMap();
    }

    if (map != nullptr) {
        m_currentMap = map->Name();

        // THIS SetMap'S ANSWER IS THE ONE RESULT DELIBERATELY NOT CHECKED,
        // and the reason is an invariant worth writing down: this function
        // runs once, before anything has been dispatched, and m_map is still
        // null when it gets here. SetMap therefore finds nothing to cancel,
        // CancelEverything returns at once, and no script code runs - so
        // there is no way for this call to end with the component destroyed.
        //
        // It matters because the callers of EnsureResolved - Asset(),
        // CurrentActionMapName(), SwitchCurrentActionMap, Dispatch - all read
        // members immediately afterwards and have no bool to check. If a path
        // is ever added that resolves while a map is already live, that is the
        // assumption it breaks, and those four are what it breaks.
        if (m_enabled) {
            m_state.SetMap(map);
        }
    }
}

const InputActionAsset* PlayerInputComponent::Asset() const {
    const_cast<PlayerInputComponent*>(this)->EnsureResolved();
    return m_asset;
}

bool PlayerInputComponent::SetActions(std::string_view virtualPath, std::string& outError) {
    // CANCELS WHATEVER THE OLD MAP HAD RUNNING - and that reaches script code,
    // which may destroy this component. False means it did, and then every
    // assignment below would be a write through a dangling `this`. Nothing has
    // been changed yet at this point, so there is nothing to undo.
    if (!m_state.SetMap(nullptr)) {
        return false;
    }

    m_actionsPath.assign(virtualPath);
    m_asset    = nullptr;
    m_resolved = false;
    m_currentMap.clear();

    if (m_actionsPath.empty()) {
        return true;
    }

    m_asset = InputActionLibrary::Load(m_actionsPath, outError);
    m_resolved = true;
    if (m_asset == nullptr) {
        return false;
    }

    // A NEW FILE STARTS ON ITS OWN DEFAULT, not on whatever the last file's
    // current map happened to be called. The two files need not share a
    // single map name.
    if (const ActionMap* map = m_asset->DefaultMap()) {
        m_currentMap = map->Name();
        if (m_enabled) {
            m_state.SetMap(map);
        }
    }
    return true;
}

void PlayerInputComponent::ReloadActions() {
    m_asset    = nullptr;
    m_resolved = false;

    // The map pointer is dropped BEFORE anything re-reads the asset, because
    // a Reload re-fills the asset's map vector and every pointer into it dies
    // with it. The current map's NAME survives, which is the whole reason the
    // name is what is kept.
    //
    // AND IT CAN COST US OUR LIFE. Dropping the map cancels what was in
    // flight, a Canceled handler is script code, and script code may remove
    // this component - so reading m_currentMap on the next line would be a
    // read through a dangling `this`. See ActionMapState's destructor.
    if (!m_state.SetMap(nullptr)) {
        return;
    }

    const std::string wanted = m_currentMap;
    EnsureResolved();
    if (!wanted.empty()) {
        SwitchCurrentActionMap(wanted);
    }
}

// ---------------------------------------------------------------------------

std::string PlayerInputComponent::CurrentActionMapName() const {
    const_cast<PlayerInputComponent*>(this)->EnsureResolved();
    return m_currentMap;
}

void PlayerInputComponent::SetDefaultMapName(std::string_view name) {
    m_defaultMap.assign(name);
    // Best effort. A name that this asset does not have is still worth
    // storing - somebody may be about to point the component at the file
    // that does have it - and SwitchCurrentActionMap says so in the log
    // without disturbing the map that is working.
    SwitchCurrentActionMap(name);
}

bool PlayerInputComponent::SwitchCurrentActionMap(std::string_view name) {
    EnsureResolved();
    if (m_asset == nullptr) {
        return false;
    }

    const ActionMap* map = m_asset->FindMap(name);
    if (map == nullptr) {
        // THE CURRENT MAP IS LEFT ALONE. Switching to a map that is not there
        // and ending up with no controls at all is worse than carrying on
        // with the ones that were working.
        ENGINE_LOG_WARN(Channels::kInput,
                        "SwitchCurrentActionMap('{}'): '{}' has no map by that name; "
                        "staying on '{}'",
                        name, m_actionsPath, m_currentMap);
        return false;
    }

    m_currentMap.assign(name);
    if (m_enabled) {
        m_state.SetMap(map);
    }
    return true;
}

void PlayerInputComponent::Enable() {
    if (m_enabled) {
        return;
    }
    m_enabled = true;
    EnsureResolved();
    if (m_asset != nullptr && !m_currentMap.empty()) {
        m_state.SetMap(m_asset->FindMap(m_currentMap));
    }
}

void PlayerInputComponent::Disable() {
    if (!m_enabled) {
        return;
    }
    m_enabled = false;
    // Cancels everything in flight. The map NAME is kept, so Enable puts the
    // same one back rather than falling to the file's default.
    m_state.SetMap(nullptr);
}

// ---------------------------------------------------------------------------

float PlayerInputComponent::Value(std::string_view action) const {
    return m_state.Value(action);
}

Vec2 PlayerInputComponent::Vector(std::string_view action) const {
    return m_state.Vector(action);
}

bool PlayerInputComponent::IsDown(std::string_view action) const {
    return m_state.IsDown(action);
}

void PlayerInputComponent::Dispatch() {
    if (!m_enabled) {
        return;
    }
    EnsureResolved();
    m_state.Dispatch();
}

void PlayerInputComponent::ResetState() { m_state.Reset(); }

// ===========================================================================

void PlayerInputSystem::Register(PlayerInputComponent& component) {
    g_players.push_back(&component);
}

void PlayerInputSystem::Unregister(PlayerInputComponent& component) {
    // TOMBSTONED RATHER THAN ERASED WHILE A WALK IS IN PROGRESS. The walks
    // null-check already, and the guard's sweep drops the tombstone as the
    // outermost walk ends, so it is gone by the time the walk is. See the note
    // on g_walkDepth for what an erase under a live cursor costs.
    //
    // AND ERASED WHEN NOTHING IS WALKING, which is not an optimisation. In
    // EDIT MODE no walk ever runs - FrameDispatch is on the simulate path - so
    // a tombstone written there would never meet a sweep, and a session of
    // adding and deleting Player Input components would grow this vector
    // without bound and never shrink it. Outside a walk there is no cursor to
    // protect, so the plain erase is both correct and self-limiting.
    if (g_walkDepth > 0) {
        PlayerInputComponent* const dead = nullptr;
        std::replace(g_players.begin(), g_players.end(), &component, dead);
    } else {
        std::erase(g_players, &component);
    }
}

void PlayerInputSystem::Clear() { g_players.clear(); }

// NOT A size(), AND THAT IS THE TOMBSTONE SHOWING THROUGH. While a walk is in
// progress this list can hold nullptr entries for components that have just
// been unregistered, so size() would over-report to anything that asked from
// inside a handler. This is introspection for the editor and the tests rather
// than frame work, and the list holds a handful of entries, so counting is the
// right price for an answer that is never wrong.
std::size_t PlayerInputSystem::Count() {
    return static_cast<std::size_t>(
        std::count_if(g_players.begin(), g_players.end(),
                      [](const PlayerInputComponent* player) {
                          return player != nullptr;
                      }));
}

void PlayerInputSystem::FrameDispatch() {
    // WALKED BY INDEX WITH THE SIZE RE-READ, and tolerating nulls, because a
    // handler reached from Dispatch is script code: it may switch maps,
    // attach a component, or destroy the entity it is standing on.
    //
    // AND COUNTED IN, which is the half that makes the null-tolerance mean
    // something: without the guard, a component unregistered by a handler is
    // erased rather than nulled and the entry after it is stepped over. See
    // g_walkDepth.
    const WalkScope walking;

    for (std::size_t i = 0; i < g_players.size(); ++i) {
        PlayerInputComponent* player = g_players[i];
        if (player == nullptr || !player->OwnerIsActive()) {
            continue;
        }
        player->Dispatch();
    }
}

void PlayerInputSystem::ResetAll() {
    // COUNTED IN TOO, and not from an abundance of caution: Reset itself
    // raises nothing, but it is reached from the editor's play/stop
    // transitions, and a component destroyed from a handler during one of
    // those would otherwise move this cursor. The guard costs an int.
    const WalkScope walking;

    for (std::size_t i = 0; i < g_players.size(); ++i) {
        if (g_players[i] != nullptr) {
            g_players[i]->ResetState();
        }
    }
}

void PlayerInputSystem::ReloadAll(std::string_view virtualPath) {
    // LOOKED UP ONCE, BEFORE THE WALK. Reload re-fills the asset in place, so
    // the asset OBJECT outlives the reload even though every pointer INTO it
    // does not - which is what makes identity the reliable half of the match
    // here. Null when the file is not in the library at all, and then only
    // the path half can match.
    const InputActionAsset* const asset = InputActionLibrary::Find(virtualPath);

    // AND COUNTED IN LIKE THE OTHER TWO. An earlier version of this comment
    // claimed ReloadActions "never registers or unregisters, so nothing moves
    // under this one", and that was wrong: ReloadActions drops the map pointer
    // with SetMap(nullptr), SetMap cancels everything the old map had in
    // flight, and a Canceled is delivered to subscribers - of which Route is
    // one. So this walk runs script code exactly like FrameDispatch does, and
    // a handler that destroys a Player Input while the Actions window saves
    // would move this cursor.
    const WalkScope walking;

    for (std::size_t i = 0; i < g_players.size(); ++i) {
        PlayerInputComponent* player = g_players[i];
        if (player == nullptr) {
            continue;
        }

        const bool holdsIt = asset != nullptr && player->Asset() == asset;
        const bool namesIt = !player->ActionsPath().empty() &&
                             player->ActionsPath() == virtualPath;
        if (holdsIt || namesIt) {
            player->ReloadActions();
        }
    }
}

void PlayerInputSystem::RegisterComponentTypes() {
    ComponentFactory::Register(PlayerInputComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<PlayerInputComponent>();
                               });
}

} // namespace eng
