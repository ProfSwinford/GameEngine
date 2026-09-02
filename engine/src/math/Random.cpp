// =============================================================================
//  Random.cpp - A SHELL. The declarations are real; the bodies are yours to write.
//
//  Everything here compiles and links, so the editor builds and runs from day
//  one. It just does not do this part yet: each function returns a harmless
//  neutral value so nothing crashes and nothing lies about having worked.
//
//  Fill these in as the course reaches them. The header this file implements
//  explains WHAT each function is for and WHY it exists - read it first.
// =============================================================================

#include <engine/math/Random.h>

namespace eng {

// TODO: all of these draw from m_engine, which the header already set up.
int   Random::NextInt(int lo, int /*hiInclusive*/) { return lo; }
float Random::NextFloat01()                        { return 0.0f; }
float Random::NextRange(float lo, float /*hi*/)    { return lo; }
bool  Random::NextBool()                           { return false; }

Random::UnitVector Random::NextDirection() { return Random::UnitVector{1.0f, 0.0f}; }

// The one shared generator. Given, because "a variable inside a function" is
// the pattern that makes it exist before anything can use it - see Subsystem.h.
Random& GlobalRandom() {
    static Random instance;
    return instance;
}

} // namespace eng
