// =============================================================================
//  DeferredOps.cpp - A SHELL. The declarations are real; the bodies are yours.
//
//  Everything here compiles and links, so the editor builds and runs from day
//  one. It just does not do this part yet: each function returns a harmless
//  neutral value so nothing crashes and nothing pretends to have worked.
//
//  Fill these in as the course reaches them. DeferredOps.h explains WHAT each
//  function is for and WHY it exists - read it first.
// =============================================================================

#include <engine/scene/DeferredOps.h>

namespace eng {

// TODO: creating and destroying entities has to be QUEUED and applied at ONE
// known point in the frame - stage 600, after every system has run and after
// messages have been delivered.
//
// DeferredOps.h explains the problem in full: a system spawning a bullet while
// something is walking the entity list changes the very list being walked, and
// in C++ that is not an error - it is a crash months later.
void DeferredOps::QueueSpawn(const SpawnParams& /*params*/) {}

void DeferredOps::QueueSpawn(SpawnBuilder /*builder*/) {}

void DeferredOps::QueueDestroy(EntityId /*id*/) {}

bool DeferredOps::IsPendingDestroy(EntityId /*id*/) { return false; }

// TODO: drain both queues, ONCE. Anything spawned while draining belongs to
// the next frame - draining until empty risks never finishing, because a
// script that spawns a copy of itself is a reasonable thing to write.
void DeferredOps::Apply(Scene& /*scene*/) {}

void DeferredOps::Clear() {}

std::size_t DeferredOps::PendingSpawnCount() { return 0; }

std::size_t DeferredOps::PendingDestroyCount() { return 0; }

} // namespace eng
