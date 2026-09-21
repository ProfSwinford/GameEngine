// =============================================================================
//  Subsystem.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. Subsystem.h is the specification; read it first.
// =============================================================================

#include <engine/core/Subsystem.h>

namespace eng {

// Adds a subsystem to the end of the list. The order they are added in IS the
// order they start in, and each one may assume everything before it is running.
void SubsystemStack::Add(std::string name, Subsystem& subsystem) {
}

// Starts every subsystem in order. If one fails, everything already started is
// shut down in reverse and this returns false - so a half-started engine never
// escapes this function.
bool SubsystemStack::InitAll(const BootConfig& config) {
    return false;
}

// Stops every started subsystem in the exact reverse of the order it was
// started in.
void SubsystemStack::ShutdownAll() {
}

} // namespace eng
