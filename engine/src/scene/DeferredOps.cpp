// =============================================================================
//  DeferredOps.cpp - a skeleton. Every function is here with the right
//  signature and an empty body. DeferredOps.h is the specification; read it
//  first.
//
//  Nothing structural happens immediately. Destroying goes into a queue applied
//  at ONE point in the frame, because removing an entity while a system is
//  walking its list is not an error in C++ - it is a crash months later.
// =============================================================================

#include <engine/scene/DeferredOps.h>

namespace eng {

// Asks for an entity to be destroyed at the end of the current step. Asking
// twice is harmless, because game code does it constantly.
void DeferredOps::QueueDestroy(EntityId /*id*/) {
}

// Is this entity already on its way out? Systems that must not act on something
// already dying check this - a destroyed thing should stop colliding at once.
bool DeferredOps::IsPendingDestroy(EntityId /*id*/) {
    return false;
}

// Applies everything queued, once. Anything queued while draining belongs to
// the next frame - draining until empty risks never finishing, because
// something that spawns a copy of itself is reasonable to write.
void DeferredOps::Apply(Scene& /*scene*/) {
}

// Throws the queue away without applying it, used when a scene is unloaded.
void DeferredOps::Clear() {
}

// How many destroys are waiting.
std::size_t DeferredOps::PendingDestroyCount() {
    return 0;
}

} // namespace eng
