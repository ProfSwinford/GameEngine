#pragma once

// ============================================================================
//  WorldUnit.h - how big "one" is.
//
//  THE PROBLEM THIS SOLVES. Every size in this engine is in pixels, which is
//  honest and completely unhelpful as a starting point. A collider authored as
//  "half extents 16 by 16" tells you nothing about whether that is big; it is
//  big relative to a 32-pixel tile and tiny relative to a 512-pixel boss. So
//  every authored size had to be typed out in full, against a mental picture
//  of the art, every time.
//
//  A WORLD UNIT IS THE SIZE OF ONE ORDINARY THING. One tile. One standing
//  character's width. The grid square the game is built on. Sizes can then be
//  written as fractions of it - 1 means "a tile", 0.5 means "half a tile" -
//  and those numbers survive a change of art resolution, which pixel counts
//  do not.
//
//  WHY IT IS A SETTING AND NOT A CONSTANT. 32 is right for this project and
//  wrong for the next one. A game drawn at 16-pixel tiles and a game drawn at
//  64 are the same game with different art, and neither should have to divide
//  every number in every scene file by two.
//
//  WHERE IT COMES FROM. config/engine.json, "units" -> "pixelsPerUnit",
//  applied once at start-up. It belongs in a project settings window
//  eventually; the file is where it lives until there is one.
//
//  WHAT USES IT. Colliders, for the size they are a fraction OF when the
//  entity has no sprite to measure instead - see physics/Collider.h - and the
//  editor, for the size a newly created object is given regardless of how
//  many pixels across its picture happens to be.
//
//  NOT A UNIT CONVERSION. Nothing is stored in units. Positions, velocities,
//  gravity and every existing size remain in pixels and are untouched by this.
//  This is one number that authoring tools multiply BY; it is not a second
//  coordinate system for the engine to keep in step with the first.
// ============================================================================

#include <engine/math/Vec2.h>

namespace eng {

class WorldUnit {
public:
    // 32 pixels. The size of this project's tiles, and of the square a new
    // object is created at.
    static constexpr float kDefaultPixels = 32.0f;

    // How many pixels one unit is. Never zero or negative - see SetPixels.
    static float Pixels();

    // A square one unit on a side, which is the shape most callers actually
    // want: "the size of an ordinary thing" rather than "a number".
    static Vec2 Square() {
        const float px = Pixels();
        return Vec2{px, px};
    }

    // CLAMPED, not trusted. This is read from a file a person edits, and
    // everything that uses a world unit DIVIDES by it: a zero here would turn
    // every authored size into an infinity and every collider into a shape
    // the solver cannot reason about. A refusal to start would be worse - a
    // mistyped settings value should not make the project unopenable - so the
    // value is pulled up to something usable and the caller reports it.
    static void SetPixels(float pixels);
};

} // namespace eng
