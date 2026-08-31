#pragma once

// =============================================================================
//  WEEK 10 - entity messaging. Ch. 8.7. *** BUILT BEFORE THE COLLIDERS. ***
//
//  Entities need to tell each other things. A bullet hits an enemy and the
//  enemy needs to lose health. The direct approach - the bullet calls
//  enemy->TakeDamage() - requires the bullet to know what an enemy IS, and
//  every new pairing adds another dependency until everything includes
//  everything.
//
//  A message is a NAMED EVENT with a payload, and the sender does not know or
//  care who is listening. Same decoupling the component model gave structure,
//  applied to communication.
//
//  Messages are identified by StringId so a C# layer can eventually send and
//  receive them BY NAME. That is not incidental - it is why messaging is worth
//  building rather than calling methods directly.
//
// =============================================================================
//  *** THE THREE DECISIONS, WRITTEN DOWN. ***
//
//  1. QUEUED, NOT IMMEDIATE.
//
//     Immediate dispatch is simpler to reason about but can recurse - A tells
//     B which tells A - so it needs a depth limit, and the recursion happens
//     in the middle of whatever system was iterating when the send happened.
//     Queued means every handler runs at ONE KNOWN POINT (stage 500,
//     CollisionResponse) with nothing mid-iteration.
//
//     The cost is that the effect lands one stage later than the send, which
//     within a single tick is invisible to a player. Queued wins because the
//     alternative reintroduces exactly the mid-iteration mutation that
//     DeferredOps exists to prevent, one file over.
//
//     SendImmediate exists for the rare case that genuinely needs it, with the
//     depth limit that implies.
//
//  2. CAN A HANDLER DESTROY THE ENTITY IT IS HANDLING A MESSAGE FOR? YES, and
//     gameplay code does it constantly - "on damage, if health <= 0, destroy
//     me." It is safe because the destroy goes through DeferredOps, which
//     applies at stage 600, AFTER dispatch at stage 500. The entity stays
//     valid for the rest of the handler and for every other handler on the
//     same message. That path is exercised by the 1000-frame stress run.
//
//  3. A MESSAGE SENT TO A DESTROYED ENTITY IS SILENTLY DROPPED, with a Debug
//     log line naming the handle. Not a crash, and not an Error either - it is
//     a completely ordinary race between two bullets hitting the same enemy in
//     one tick, and an Error-level line per occurrence would drown the log
//     during exactly the situation you were trying to observe.
//
//  UNSUBSCRIBING FROM INSIDE A HANDLER works. A subscription is marked dead
//  and the list is compacted after dispatch finishes, never during - the same
//  iterator-invalidation problem as DeferredOps, in a smaller box, solved the
//  same way.
// =============================================================================

#include <engine/core/StringId.h>
#include <engine/scene/Entity.h>

#include <functional>

namespace eng {

struct Message {
    StringId     type;
    EntityHandle sender{};
    EntityHandle target{};

    // A SMALL payload, deliberately. Two floats, an int and a handle covers
    // every message Phase 1 and Phase 2 need: damage amounts, collision
    // partners, trigger ids. A general variant payload is a Phase 2 project,
    // not a Week 10 one, and every field added here is paid for by every
    // message ever queued.
    f32          f0 = 0.0f;
    f32          f1 = 0.0f;
    i32          i0 = 0;
    EntityHandle other{};
};

using MessageHandler = std::function<void(const Message&)>;
using SubscriptionId = u32;

// The message types the engine itself sends. Gameplay may define its own; the
// type is just a StringId.
namespace MessageTypes {
StringId CollisionEnter();
StringId CollisionStay();
StringId CollisionExit();
} // namespace MessageTypes

class MessageBus {
public:
    // Listen for one message type on one entity.
    static SubscriptionId Subscribe(EntityHandle target, StringId type, MessageHandler handler);
    // Listen for one message type from anywhere. Used by the gate game's
    // score keeper, and by the editor's collision log.
    static SubscriptionId SubscribeBroadcast(StringId type, MessageHandler handler);
    // Safe to call from inside a handler.
    static void Unsubscribe(SubscriptionId id);
    static void UnsubscribeAll(EntityHandle target);

    // Queued. The handler runs at the dispatch point, not here.
    static void Send(const Message& message);
    static void Broadcast(const Message& message);

    // Runs the handler now, on this stack. Depth-limited; a recursion deeper
    // than kMaxImmediateDepth is dropped with an Error, because an unbounded
    // A-tells-B-tells-A is a stack overflow with no diagnostic.
    static void SendImmediate(const Message& message);
    static constexpr u32 kMaxImmediateDepth = 8;

    // Drains the queue. Called once per simulation step at stage 500.
    static void Dispatch();

    static void Clear();

    // Instrumentation for the editor.
    static usize QueuedCount();
    static usize SubscriptionCount();
    static u64   TotalDispatched();
};

} // namespace eng
