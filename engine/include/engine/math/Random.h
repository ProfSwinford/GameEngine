#pragma once

// ============================================================================
//  Random.h - random numbers for gameplay.
//
//  WHY NOT rand()
//  The old C function rand() has one shared hidden state, a small range, and
//  quality that varies between compilers. C++ replaced it with the <random>
//  header, and that is what this class wraps.
//
//  WHY WRAP <random> AT ALL INSTEAD OF USING IT DIRECTLY
//  <random> is powerful but wordy: to get "a number between 1 and 6" you have
//  to create a generator, create a distribution, and then combine them. That
//  is worth learning eventually, and it is noise in the middle of gameplay
//  code. This class does it once, here, and gives you:
//
//      eng::Random dice;
//      int roll = dice.NextInt(1, 6);
//
//  WHY A SEED
//  A generator started from the same seed produces the same sequence of
//  numbers every time. That turns "it only crashes sometimes" into a bug you
//  can reproduce on demand, which is the difference between a fixable problem
//  and a haunted one. Each Random remembers its seed so it can be printed.
// ============================================================================

#include <random>

namespace eng {

class Random {
public:
    // An arbitrary fixed number, deliberately NOT the current time. A game
    // that seeds itself from the clock by default behaves differently on every
    // run, which makes bugs impossible to reproduce. Seeding from the clock is
    // something a caller should do on purpose and then log.
    static constexpr unsigned int kDefaultSeed = 12345u;

    Random() : Random(kDefaultSeed) {}
    explicit Random(unsigned int seed) : m_engine(seed), m_seed(seed) {}

    // A whole number from lo to hiInclusive, both ends possible.
    // NextInt(1, 6) is a six-sided die.
    int NextInt(int lo, int hiInclusive);

    // A decimal from 0 up to (but never exactly) 1.
    float NextFloat01();

    // A decimal somewhere between lo and hi.
    float NextRange(float lo, float hi);

    // A coin flip.
    bool NextBool();

    // A direction: a vector of length 1 pointing at a random angle. Handy for
    // scattering particles or picking a starting heading.
    struct UnitVector { float x, y; };
    UnitVector NextDirection();

    // Restarts the sequence from a new seed.
    void Reseed(unsigned int seed) { m_engine.seed(seed); m_seed = seed; }

    unsigned int Seed() const { return m_seed; }

private:
    // std::mt19937 is the Mersenne Twister, the standard library's general
    // purpose generator. It is the one to reach for unless you have a specific
    // reason not to: good statistical quality, well tested, and its sequence
    // is defined by the standard, so a given seed behaves the same everywhere.
    std::mt19937 m_engine;
    unsigned int m_seed = kDefaultSeed;
};

// One shared generator for code that just wants a random number and does not
// care about reproducing it. Anything that DOES care should make its own
// Random with its own seed.
//
// It is a function rather than a plain global variable on purpose: a global is
// created at an unpredictable moment during program start-up, whereas the
// variable inside this function is created the first time somebody calls it,
// which is by definition after everything it needs already exists.
Random& GlobalRandom();

} // namespace eng
