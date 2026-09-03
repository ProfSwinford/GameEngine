// =============================================================================
//  Random.cpp - a skeleton. Every function is here with the right signature and
//  an empty body. Random.h is the specification; read it before filling one in.
// =============================================================================

#include <engine/math/Random.h>

namespace eng {

// A whole number from lo to hiInclusive, both ends possible. NextInt(1, 6) is a
// six-sided die.
int Random::NextInt(int lo, int /*hiInclusive*/) {
    return lo;
}

// A decimal from 0 up to, but never exactly, 1.
float Random::NextFloat01() {
    return 0.0f;
}

// A decimal somewhere between lo and hi.
float Random::NextRange(float lo, float /*hi*/) {
    return lo;
}

// A coin flip.
bool Random::NextBool() {
    return false;
}

// A direction: a vector of length 1 pointing at a random angle. Useful for
// scattering things without them drifting towards one corner.
Random::UnitVector Random::NextDirection() {
    return Random::UnitVector{1.0f, 0.0f};
}

// The one shared generator, for code that does not need its own sequence.
// Seeded from a fixed number rather than the clock, so a bug can be reproduced.
Random& GlobalRandom() {
    static Random instance;
    return instance;
}

} // namespace eng
