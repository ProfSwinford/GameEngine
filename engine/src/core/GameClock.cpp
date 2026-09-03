// =============================================================================
//  GameClock.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. GameClock.h is the specification; read it first.
// =============================================================================

#include <engine/core/GameClock.h>

namespace eng {

// Starts the clock from zero and empties the accumulator.
void GameClock::Init() {
}

// Sets how much game time one simulation step represents. Every step is this
// long on every machine, which is what makes the game behave the same on all
// of them.
void GameClock::SetFixedStepSeconds(float /*seconds*/) {
}

// Speeds game time up or slows it down. It does not change the size of a step -
// only how many of them a second of real time is worth.
void GameClock::SetTimeScale(float /*scale*/) {
}

// Caps how many steps one frame may run. Without a ceiling, a frame that runs
// slow asks for more steps, which makes it slower still, and the program locks
// up and never recovers.
void GameClock::SetMaxStepsPerFrame(int /*steps*/) {
}

// Adds this frame's real time to the accumulator and returns how many whole
// fixed steps the simulation now owes. Usually 0, 1 or 2.
int GameClock::BeginFrame(double /*realDeltaSeconds*/) {
    return 0;
}

// Records that one owed step has been run, taking its time back out of the
// accumulator.
void GameClock::OnStepConsumed() {
}

} // namespace eng
