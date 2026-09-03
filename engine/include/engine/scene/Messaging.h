#pragma once

// ============================================================================
//  Messaging.h - how entities tell each other things.
//
//  A bullet hits an enemy and the enemy needs to lose health. The direct
//  approach is for the bullet to call enemy->TakeDamage(), which means the
//  bullet's code has to know what an enemy IS - and every new pairing adds
//  another such dependency until everything includes everything else.
//
//  A message is a NAMED EVENT with a little bit of data attached. The sender
//  does not know or care who is listening. That is the same decoupling the
//  component model gives structure, applied to communication.
//
//  ==========================================================================
//  THREE RULES, WRITTEN DOWN
//
//  1. MESSAGES ARE QUEUED, NOT DELIVERED IMMEDIATELY.
//
//     Delivering straight away is simpler to picture but it can loop - A tells
//     B, which tells A - and it happens in the middle of whatever system was
//     running when the message was sent. Queueing means every handler runs at
//     ONE known point (stage 500 in the system order) with nothing halfway
//     through anything.
//
//     The cost is that the effect lands a fraction of a tick later than the
//     send, which no player can perceive.
//
//  2. A HANDLER MAY DESTROY THE ENTITY IT IS HANDLING A MESSAGE FOR.
//     Game code does this constantly - "on damage, if health is zero, destroy
//     me". It is safe because destroying goes through DeferredOps, which runs
//     at stage 600, AFTER delivery at stage 500. The entity stays valid for
//     the rest of that handler and for every other handler on the same message.
//
//  3. A MESSAGE SENT TO AN ENTITY THAT NO LONGER EXISTS IS QUIETLY DROPPED.
//     Not a crash, and not an error either - two bullets hitting the same
//     enemy on one tick is completely ordinary, and an error message per
//     occurrence would bury the Console during exactly the moment you were
//     trying to watch.
//
//  UNSUBSCRIBING FROM INSIDE A HANDLER WORKS. A subscription is marked dead
//  and the list is tidied up after delivery finishes, never during - the same
//  problem DeferredOps solves for entities, in a smaller box.
//  ==========================================================================
// ============================================================================

#include <engine/scene/EntityId.h>

#include <functional>
#include <string>

namespace eng {

struct Message {
    std::string type;       // e.g. "CollisionEnter", "Damage"
    EntityId    sender{};
    EntityId    target{};   // null means "everybody listening"

    // A deliberately SMALL payload. Two decimals, a whole number and one more
    // entity covers everything this engine needs to say: how much damage, what
    // was hit, which trigger. A general "any value at all" payload is a much
    // bigger piece of machinery, and every field added here is paid for by
    // every message ever sent.
    float    f0 = 0.0f;
    float    f1 = 0.0f;
    int      i0 = 0;
    EntityId other{};
};

// std::function is the standard "any callable thing" type: a plain function, a
// lambda, or a lambda that captured some state all fit in one. That is what
// lets a subscriber be written inline at the point where it is registered.
using MessageHandler = std::function<void(const Message&)>;

// Handed back when you subscribe, so you can unsubscribe later.
using SubscriptionId = unsigned int;

// The messages the engine itself sends. Game code is free to invent its own -
// a message type is just a name.
namespace MessageTypes {
inline constexpr const char* kCollisionEnter = "CollisionEnter";
inline constexpr const char* kCollisionStay  = "CollisionStay";
inline constexpr const char* kCollisionExit  = "CollisionExit";
} // namespace MessageTypes

class MessageBus {
public:
    // Listen for one kind of message, wherever it came from and whoever it was
    // aimed at. There is only one kind of subscription: a handler that cares
    // about one particular entity compares message.target itself, which is
    // exactly what the script system does.
    static SubscriptionId SubscribeBroadcast(std::string_view type,
                                             MessageHandler handler);

    // Safe to call from inside a handler.
    static void Unsubscribe(SubscriptionId id);

    // Queue a message. The handlers run at the delivery point, not here.
    static void Send(const Message& message);

    // Delivers everything queued. Called once per simulation step, at stage 500.
    static void Dispatch();

    static void Clear();
};

} // namespace eng
