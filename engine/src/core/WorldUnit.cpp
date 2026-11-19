// ============================================================================
//  WorldUnit.cpp - the one number. See WorldUnit.h.
// ============================================================================

#include <engine/core/WorldUnit.h>

#include <algorithm>

namespace eng {
namespace {

// A FUNCTION-LOCAL STATIC, not a file-scope one, so it is initialised the
// first time it is asked for rather than at some point during static
// initialisation that nothing here controls. A collider built by a scene
// loaded from a global constructor would otherwise be measured against a
// zero.
float& Storage() {
    static float pixels = WorldUnit::kDefaultPixels;
    return pixels;
}

} // namespace

float WorldUnit::Pixels() {
    return Storage();
}

void WorldUnit::SetPixels(float pixels) {
    // ONE PIXEL IS THE FLOOR. Anything smaller is either a typo or a request
    // for sizes that cannot be drawn, and both are better served by a number
    // that works than by one that propagates. See the note in the header.
    Storage() = std::max(pixels, 1.0f);
}

} // namespace eng
