// ============================================================================
//  Messaging.cpp - the message bus. See Messaging.h for the three rules.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/scene/Messaging.h>
#include <engine/scene/Scene.h>

#include <algorithm>
#include <deque>
#include <memory>
#include <vector>

namespace eng {
namespace {

struct Subscription {
    SubscriptionId id = 0;
    std::string    type;
    EntityId       target{};    // null when this is a broadcast listener
    MessageHandler handler;
    bool           alive     = true;
    bool           broadcast = false;
};

// A list of unique_ptrs rather than a list of Subscriptions by value, for one
// specific reason: a handler is allowed to subscribe while it is running, and
// adding to a vector can move all of its contents somewhere else in memory -
// which would destroy the handler CURRENTLY EXECUTING. Holding pointers means
// each Subscription stays where it is; only the small array of pointers moves.
std::vector<std::unique_ptr<Subscription>> g_subscriptions;

// std::deque rather than std::vector because messages are added at the back
// and taken from the FRONT, and a deque can do both cheaply.
std::deque<Message> g_queue;

SubscriptionId g_nextId      = 1;
bool           g_dispatching = false;

void Compact() {
    // Only ever called when delivery is NOT in progress. Erasing while the
    // delivery loop is walking the list by index would make it skip entries -
    // the same problem DeferredOps exists to solve, in a smaller box.
    std::erase_if(g_subscriptions, [](const std::unique_ptr<Subscription>& s) {
        return s == nullptr || !s->alive;
    });
}

void DeliverTo(const Subscription& subscription, const Message& message) {
    if (!subscription.alive || !subscription.handler) {
        return;
    }
    if (subscription.type != message.type) {
        return;
    }
    // A targeted subscription only wants messages aimed at its own entity;
    // a broadcast subscription takes them all.
    if (!subscription.broadcast && subscription.target != message.target) {
        return;
    }
    subscription.handler(message);
}

bool TargetStillExists(const Message& message) {
    if (message.target.IsNull()) {
        return true;   // a broadcast has no particular target
    }
    Scene* scene = Scene::Active();
    if (scene == nullptr) {
        return false;
    }
    // RULE 3: quietly dropped. This is a normal thing to happen, not an error.
    return scene->IsValid(message.target);
}

} // namespace

SubscriptionId MessageBus::Subscribe(EntityId target, std::string_view type,
                                     MessageHandler handler) {
    auto subscription       = std::make_unique<Subscription>();
    subscription->id        = g_nextId++;
    subscription->type      = std::string(type);
    subscription->target    = target;
    subscription->handler   = std::move(handler);
    subscription->broadcast = false;

    const SubscriptionId id = subscription->id;
    g_subscriptions.push_back(std::move(subscription));
    return id;
}

SubscriptionId MessageBus::SubscribeBroadcast(std::string_view type,
                                              MessageHandler handler) {
    auto subscription       = std::make_unique<Subscription>();
    subscription->id        = g_nextId++;
    subscription->type      = std::string(type);
    subscription->handler   = std::move(handler);
    subscription->broadcast = true;

    const SubscriptionId id = subscription->id;
    g_subscriptions.push_back(std::move(subscription));
    return id;
}

void MessageBus::Unsubscribe(SubscriptionId id) {
    // MARKED DEAD, never removed here. That is what makes unsubscribing from
    // inside a handler safe - and a handler that destroys its own entity does
    // exactly that.
    for (auto& subscription : g_subscriptions) {
        if (subscription != nullptr && subscription->id == id) {
            subscription->alive = false;
            break;
        }
    }
    if (!g_dispatching) {
        Compact();
    }
}

void MessageBus::UnsubscribeAll(EntityId target) {
    for (auto& subscription : g_subscriptions) {
        if (subscription != nullptr && !subscription->broadcast &&
            subscription->target == target) {
            subscription->alive = false;
        }
    }
    if (!g_dispatching) {
        Compact();
    }
}

void MessageBus::Send(const Message& message) {
    g_queue.push_back(message);
}

void MessageBus::Broadcast(const Message& message) {
    Message copy = message;
    copy.target  = EntityId{};   // no target, so every broadcast listener sees it
    g_queue.push_back(copy);
}

void MessageBus::Dispatch() {
    g_dispatching = true;

    // Drained ONCE. The number of messages is read before the loop starts, so
    // a handler that sends another message queues it for the NEXT tick rather
    // than extending this pass. Draining until empty risks never finishing
    // when two handlers keep answering each other.
    const std::size_t count = g_queue.size();

    for (std::size_t processed = 0; processed < count && !g_queue.empty(); ++processed) {
        const Message message = g_queue.front();
        g_queue.pop_front();

        if (!TargetStillExists(message)) {
            continue;
        }

        // Walked by index and the size re-read each time, because a handler is
        // allowed to add a subscription while it runs.
        for (std::size_t i = 0; i < g_subscriptions.size(); ++i) {
            if (g_subscriptions[i] != nullptr) {
                DeliverTo(*g_subscriptions[i], message);
            }
        }
    }

    g_dispatching = false;
    Compact();
}

void MessageBus::Clear() {
    g_subscriptions.clear();
    g_queue.clear();
    g_dispatching = false;
}

std::size_t MessageBus::QueuedCount()       { return g_queue.size(); }
std::size_t MessageBus::SubscriptionCount() { return g_subscriptions.size(); }

} // namespace eng
