// WEEK 10 - the message bus. See Messaging.h for the three recorded decisions.

#include <engine/core/Log.h>
#include <engine/scene/DeferredOps.h>
#include <engine/scene/Messaging.h>
#include <engine/scene/Scene.h>

#include <algorithm>
#include <deque>
#include <memory>
#include <vector>

namespace eng {
namespace {

struct Subscription {
    SubscriptionId id      = 0;
    StringId       type;
    EntityHandle   target{};      // null means broadcast
    MessageHandler handler;
    bool           alive   = true;
    bool           broadcast = false;
};

// unique_ptr, not Subscription by value, and the reason is specific: a
// handler may subscribe while it is running, and a push_back that reallocated
// a vector<Subscription> would destroy the std::function CURRENTLY EXECUTING
// on this stack. Indirection gives every Subscription a stable address; only
// the pointer array moves, and the loops re-index each step so that is safe.
std::vector<std::unique_ptr<Subscription>> g_subscriptions;
std::deque<Message>       g_queue;
SubscriptionId            g_nextId    = 1;
u64                       g_dispatched = 0;
u32                       g_immediateDepth = 0;
bool                      g_dispatching = false;

void Compact() {
    // Only ever called OUTSIDE dispatch. Erasing a subscription while the
    // dispatch loop holds an index into the vector is the same
    // iterator-invalidation bug DeferredOps exists to prevent - this is that
    // problem in a smaller box, solved the same way.
    std::erase_if(g_subscriptions,
                  [](const std::unique_ptr<Subscription>& subscription) {
                      return subscription == nullptr || !subscription->alive;
                  });
}

void DeliverTo(const Subscription& subscription, const Message& message) {
    if (!subscription.alive || !subscription.handler) {
        return;
    }
    if (subscription.type != message.type) {
        return;
    }
    if (!subscription.broadcast && subscription.target != message.target) {
        return;
    }
    subscription.handler(message);
}

bool TargetStillExists(const Message& message) {
    if (message.target.IsNull()) {
        return true;   // a broadcast has no target
    }
    Scene* scene = Scene::Active();
    if (scene == nullptr) {
        return false;
    }
    // DECISION 3: a message to a destroyed entity is silently dropped with a
    // Debug line. Not an Error - two bullets hitting the same enemy in one
    // tick is completely ordinary, and an Error per occurrence would drown the
    // log during exactly the situation being observed.
    if (!scene->IsValid(message.target)) {
        ENGINE_LOG_DEBUG(Channels::kScene,
                         "message '{}' dropped: target entity (index {} generation {}) no "
                         "longer exists", message.type.ToString(), message.target.Index(),
                         message.target.Generation());
        return false;
    }
    return true;
}

} // namespace

namespace MessageTypes {

// Function-local statics so the ids are interned exactly once and the reverse
// table has their text - a dropped-message log line that says "message
// '<sid:0x...>'" is much less useful than one that says "CollisionEnter".
StringId CollisionEnter() { static const StringId id = Intern("CollisionEnter"); return id; }
StringId CollisionStay()  { static const StringId id = Intern("CollisionStay");  return id; }
StringId CollisionExit()  { static const StringId id = Intern("CollisionExit");  return id; }

} // namespace MessageTypes

SubscriptionId MessageBus::Subscribe(EntityHandle target, StringId type,
                                     MessageHandler handler) {
    auto subscription       = std::make_unique<Subscription>();
    subscription->id        = g_nextId++;
    subscription->type      = type;
    subscription->target    = target;
    subscription->handler   = std::move(handler);
    subscription->broadcast = false;

    const SubscriptionId id = subscription->id;
    g_subscriptions.push_back(std::move(subscription));
    return id;
}

SubscriptionId MessageBus::SubscribeBroadcast(StringId type, MessageHandler handler) {
    auto subscription       = std::make_unique<Subscription>();
    subscription->id        = g_nextId++;
    subscription->type      = type;
    subscription->handler   = std::move(handler);
    subscription->broadcast = true;

    const SubscriptionId id = subscription->id;
    g_subscriptions.push_back(std::move(subscription));
    return id;
}

void MessageBus::Unsubscribe(SubscriptionId id) {
    // MARK DEAD, never erase. This is what makes unsubscribing from inside a
    // handler safe, and a handler that destroys its own entity does exactly
    // that.
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

void MessageBus::UnsubscribeAll(EntityHandle target) {
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
    copy.target  = EntityHandle{};   // no target: every broadcast listener sees it
    g_queue.push_back(copy);
}

void MessageBus::SendImmediate(const Message& message) {
    if (g_immediateDepth >= kMaxImmediateDepth) {
        // An unbounded A-tells-B-tells-A is a stack overflow with no
        // diagnostic. This is the diagnostic.
        ENGINE_LOG_ERROR(Channels::kScene,
                         "immediate message '{}' exceeded depth {} and was dropped - "
                         "check for a handler that sends back to its sender",
                         message.type.ToString(), kMaxImmediateDepth);
        return;
    }
    if (!TargetStillExists(message)) {
        return;
    }

    ++g_immediateDepth;
    // By index and re-reading the size: a handler may subscribe, and
    // push_back would invalidate an iterator.
    for (usize i = 0; i < g_subscriptions.size(); ++i) {
        if (g_subscriptions[i] != nullptr) {
            DeliverTo(*g_subscriptions[i], message);
        }
    }
    --g_immediateDepth;
    ++g_dispatched;
}

void MessageBus::Dispatch() {
    g_dispatching = true;

    // Drained ONCE. A handler that sends another message queues it for the
    // NEXT dispatch rather than extending this one - draining in a loop until
    // empty risks never terminating when two handlers answer each other, and
    // "next tick" is a delay nobody can perceive.
    const usize count = g_queue.size();
    for (usize processed = 0; processed < count && !g_queue.empty(); ++processed) {
        const Message message = g_queue.front();
        g_queue.pop_front();

        if (!TargetStillExists(message)) {
            continue;
        }

        for (usize i = 0; i < g_subscriptions.size(); ++i) {
            if (g_subscriptions[i] != nullptr) {
                DeliverTo(*g_subscriptions[i], message);
            }
        }
        ++g_dispatched;
    }

    g_dispatching = false;
    Compact();
}

void MessageBus::Clear() {
    g_subscriptions.clear();
    g_queue.clear();
    g_dispatching = false;
}

usize MessageBus::QueuedCount()       { return g_queue.size(); }
usize MessageBus::SubscriptionCount() { return g_subscriptions.size(); }
u64   MessageBus::TotalDispatched()   { return g_dispatched; }

} // namespace eng
