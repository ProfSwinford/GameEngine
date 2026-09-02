// =============================================================================
//  Messaging.cpp - A SHELL. The declarations are real; the bodies are yours.
//
//  Everything here compiles and links, so the editor builds and runs from day
//  one. It just does not do this part yet: each function returns a harmless
//  neutral value so nothing crashes and nothing pretends to have worked.
//
//  Fill these in as the course reaches them. Messaging.h explains WHAT each
//  function is for and WHY it exists - read it first.
// =============================================================================

#include <engine/scene/Messaging.h>

namespace eng {

// TODO: remember who is listening for what. A subscription id is handed back
// so it can be cancelled later.
SubscriptionId MessageBus::Subscribe(EntityId /*target*/, std::string_view /*type*/,
                                     MessageHandler /*handler*/) {
    return 0;
}

SubscriptionId MessageBus::SubscribeBroadcast(std::string_view /*type*/,
                                              MessageHandler /*handler*/) {
    return 0;
}

void MessageBus::Unsubscribe(SubscriptionId /*id*/) {}

void MessageBus::UnsubscribeAll(EntityId /*target*/) {}

// TODO: QUEUE the message rather than delivering it. Immediate delivery can
// loop - A tells B, which tells A - and it lands in the middle of whatever
// system happened to be running. See the three rules in Messaging.h.
void MessageBus::Send(const Message& /*message*/) {}

void MessageBus::Broadcast(const Message& /*message*/) {}

// TODO: the ONE point in the frame where every handler runs - stage 500.
//
// A message aimed at an entity that no longer exists is dropped quietly, not
// reported: two bullets hitting the same enemy on one tick is ordinary, and an
// error per occurrence would bury the Console exactly when you were reading it.
void MessageBus::Dispatch() {}

void MessageBus::Clear() {}

std::size_t MessageBus::QueuedCount() { return 0; }

std::size_t MessageBus::SubscriptionCount() { return 0; }

} // namespace eng
