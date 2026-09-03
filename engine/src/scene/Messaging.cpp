// =============================================================================
//  Messaging.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. Messaging.h is the specification; read it first.
//
//  A message is a NAMED EVENT with a little data attached. The sender does not
//  know who is listening, which is what stops every pair of things in the game
//  having to include each other.
// =============================================================================

#include <engine/scene/Messaging.h>

namespace eng {

// Listens for one kind of message and hands back an id for cancelling it later.
// A handler that only cares about one entity compares message.target itself.
SubscriptionId MessageBus::SubscribeBroadcast(std::string_view /*type*/,
                                              MessageHandler /*handler*/) {
    return 0;
}

// Stops listening. Safe to call from inside a handler, which is what a handler
// that destroys its own entity ends up doing.
void MessageBus::Unsubscribe(SubscriptionId /*id*/) {
}

// Queues a message. The handlers do not run here - they run at the delivery
// point, so nothing is ever half-way through something else when they do.
void MessageBus::Send(const Message& /*message*/) {
}

// Delivers everything queued, once per simulation step. This is the only place
// in the engine where a message handler runs.
void MessageBus::Dispatch() {
}

// Throws away every queued message and every subscription.
void MessageBus::Clear() {
}

} // namespace eng
