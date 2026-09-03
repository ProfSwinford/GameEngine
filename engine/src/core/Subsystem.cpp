// =============================================================================
//  Subsystem.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. Subsystem.h is the specification; read it first.
// =============================================================================

#include <engine/core/Subsystem.h>

namespace eng {

// Adds a subsystem to the end of the list. The order they are added in IS the
// order they start in, and each one may assume everything before it is running.
void SubsystemStack::Register(std::unique_ptr<Subsystem> /*subsystem*/) {
}

// Starts every subsystem in order. If one fails, everything already started is
// shut down in reverse and this returns false - so a half-started engine never
// escapes this function.
bool SubsystemStack::InitAll() {
    return false;
}

// Stops every started subsystem in the exact reverse of the order it was
// started in.
void SubsystemStack::ShutdownAll() {
}

// Visits each subsystem with whether it is currently running, so the engine can
// print the start-up order to the log.
void SubsystemStack::ForEach(
    const std::function<void(const Subsystem&, bool)>& /*fn*/) const {
}

} // namespace eng
