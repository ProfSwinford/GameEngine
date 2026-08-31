// ============================================================================
//  Random.cpp - the random number helpers declared in Random.h.
//
//  Each function pairs the generator (m_engine, which produces raw random
//  bits) with a DISTRIBUTION (which reshapes those bits into the range and
//  spread you asked for). That two-part split is how <random> is designed:
//  generators and distributions are separate so either can be swapped without
//  touching the other.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/math/Random.h>
#include <engine/math/Vec2.h>

#include <cmath>
#include <utility>

namespace eng {

int Random::NextInt(int lo, int hiInclusive) {
    if (lo > hiInclusive) {
        // Rather than return something arbitrary, say so - a reversed range is
        // almost always a typo in the calling code - and then carry on with
        // the range the caller probably meant.
        ENGINE_LOG_WARN(Channels::kCore,
                        "Random::NextInt called with lo={} greater than hi={}; "
                        "swapping them",
                        lo, hiInclusive);
        std::swap(lo, hiInclusive);
    }

    // std::uniform_int_distribution gives every value in the range an equal
    // chance. Writing `m_engine() % range` by hand instead is very slightly
    // biased towards the low numbers and is the classic beginner mistake here.
    std::uniform_int_distribution<int> distribution(lo, hiInclusive);
    return distribution(m_engine);
}

float Random::NextFloat01() {
    // The range is written as [0, 1) - 0 is possible, 1 is not. That is the
    // convention every random-float API uses, and it is what makes
    // `array[(int)(NextFloat01() * size)]` safe.
    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
    return distribution(m_engine);
}

float Random::NextRange(float lo, float hi) {
    if (lo > hi) {
        std::swap(lo, hi);
    }
    std::uniform_real_distribution<float> distribution(lo, hi);
    return distribution(m_engine);
}

bool Random::NextBool() {
    // std::bernoulli_distribution is the standard "true with probability p"
    // distribution; 0.5 makes it a fair coin.
    std::bernoulli_distribution distribution(0.5);
    return distribution(m_engine);
}

Random::UnitVector Random::NextDirection() {
    // Pick an angle anywhere around the circle, then convert it to x and y.
    // The result always has length 1, which is what "a direction" means.
    const float angle = NextRange(0.0f, kTwoPi);
    return UnitVector{std::cos(angle), std::sin(angle)};
}

Random& GlobalRandom() {
    // A "function-local static": created the first time this function runs,
    // then reused forever after. See the note in Random.h for why this is
    // preferred over a plain global variable.
    static Random instance;
    return instance;
}

} // namespace eng
