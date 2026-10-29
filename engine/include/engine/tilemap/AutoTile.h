#pragma once

// ============================================================================
//  AutoTile.h - working out what a tile LOOKS like from the tiles around it.
//
//  Draw a wall by hand and you draw one thing. Draw a wall out of tiles and
//  you have to draw forty-seven: a straight run, the two ends, the four
//  outside corners, the four inside corners, a single isolated block, a cross
//  junction, and so on down the list. Getting one of them wrong leaves a seam
//  that is visible from across the room, and every new material means drawing
//  all forty-seven again.
//
//  This file is the algorithm that produces all forty-seven FROM FIVE, which
//  is the idea behind Fang Auto Tile (github.com/ruccho/FangAutoTile) and the
//  sheet format it inherits from WOLF RPG Editor. The format is unchanged, so
//  art drawn for either of those works here as it is.
//
//  ==========================================================================
//  THE TRICK: A TILE IS NOT ONE PICTURE, IT IS FOUR QUARTERS
//
//  Ask "which of 47 pictures is this tile?" and you need 47 pictures. Ask
//  instead, four times over, "which of 5 pictures is this QUARTER of the
//  tile?" and you need 5. The whole saving is in that change of question.
//
//  It works because a corner of a tile can only see three of the eight
//  neighbours - the two beside it and the one diagonally past it. The
//  top-left quarter of a tile cannot tell you anything about what is to the
//  south-east, and does not need to:
//
//        NW  N  NE          +-----+-----+
//         W  .  E     ->    | TL  | TR  |     TL is decided by N, W, NW
//        SW  S  SE          +-----+-----+     TR is decided by N, E, NE
//                           | BL  | BR  |     BL is decided by S, W, SW
//                           +-----+-----+     BR is decided by S, E, SE
//
//  Three neighbours is eight possibilities, and those eight collapse into
//  FIVE distinct shapes - which is the whole of the tile sheet. Taking the
//  top-left quarter as the example:
//
//    neither beside it          -> OuterCorner     a convex corner, sticking out
//    the vertical one only      -> VerticalEdge    a left or right wall
//    the horizontal one only    -> HorizontalEdge  a top or bottom edge
//    both, but not the diagonal -> InnerCorner     a concave corner, tucked in
//    all three                  -> Fill            solid interior
//
//  The two middle cases ignore the diagonal entirely, and that is why eight
//  becomes five: when there is a gap beside you, what is past the gap cannot
//  be seen from here.
//
//  ==========================================================================
//  WHY FORTY-SEVEN AND NOT 5 x 5 x 5 x 5 = 625
//
//  The four quarters are not independent, because ADJACENT QUARTERS READ THE
//  SAME NEIGHBOUR. The top-left and top-right quarters both look at N, so
//  they cannot disagree about whether N is there. Four such shared bits give
//  four constraints:
//
//      TL and TR share N        TL and BL share W
//      BL and BR share S        TR and BR share E
//
//  Count what survives and it is 47 - exactly the number of tiles that would
//  otherwise be drawn by hand. The arithmetic is worked through above
//  AutoTilePatterns() in the .cpp, and a test checks the count rather than
//  trusting it.
//
//  ==========================================================================
//  THE SHEET: ONE TILE WIDE, FIVE TILES TALL
//
//  Each band is a whole tile, and the quarter taken from it is the quarter in
//  the SAME CORNER. The top-left quarter of a tile always comes from the
//  top-left quarter of one of the five bands - never from a different corner -
//  so each band is drawn as a complete tile and simply read four times:
//
//      +--------+   y = 0 * tileSize   OuterCorner     (convex)
//      +--------+   y = 1 * tileSize   VerticalEdge    (left/right wall)
//      +--------+   y = 2 * tileSize   HorizontalEdge  (top/bottom edge)
//      +--------+   y = 3 * tileSize   InnerCorner     (concave)
//      +--------+   y = 4 * tileSize   Fill            (interior)
//
//  NOTHING IS BAKED. The quarter to draw is four numbers into the sheet, so
//  the texture is used exactly as it was loaded. Fang bakes a texture of all
//  47 combinations up front because Unity's tilemap wants one finished sprite
//  per tile; this engine draws from a source rectangle anyway, so baking would
//  be work done twice to arrive at the same pixels. QuadrantSourceRect below
//  is the whole of what baking would have produced.
//
//  EXTRA FRAMES GO SIDEWAYS. A sheet five tiles tall and four tiles wide holds
//  four frames of the same material, for animation or for random variation.
//  See TileAsset.h.
// ============================================================================

#include <engine/render/Renderer.h>

#include <array>

namespace eng {

// ---------------------------------------------------------------------------
//  The eight neighbours, as bits in one byte.
//
//  Clockwise from north, which is Fang's numbering and is kept deliberately:
//  a mask logged here means the same thing as a mask logged there, so the
//  pattern table can be compared against the original number for number.
//
//  NORTH IS +Y, because the world is y-up everywhere in this engine. A tile
//  grid is not an image - row 0 is at the BOTTOM - and the one place the two
//  are reconciled is QuadrantSourceRect, whose y counts down from the top of
//  the image like every other pixel coordinate in the engine.
// ---------------------------------------------------------------------------
namespace Neighbour {
inline constexpr unsigned char kNorth     = 1;     // ( 0, +1)
inline constexpr unsigned char kNorthEast = 2;     // (+1, +1)
inline constexpr unsigned char kEast      = 4;     // (+1,  0)
inline constexpr unsigned char kSouthEast = 8;     // (+1, -1)
inline constexpr unsigned char kSouth     = 16;    // ( 0, -1)
inline constexpr unsigned char kSouthWest = 32;    // (-1, -1)
inline constexpr unsigned char kWest      = 64;    // (-1,  0)
inline constexpr unsigned char kNorthWest = 128;   // (-1, +1)

// Every straight neighbour, and every diagonal one. A tile with all eight
// present - a mask of kAll - is the one that draws as solid interior.
inline constexpr unsigned char kStraights = kNorth | kEast | kSouth | kWest;
inline constexpr unsigned char kDiagonals =
    kNorthEast | kSouthEast | kSouthWest | kNorthWest;
inline constexpr unsigned char kAll = kStraights | kDiagonals;

// The bit for one step, or 0 for a step that is not a neighbour at all -
// including (0, 0), because a tile is not its own neighbour.
unsigned char Bit(int dx, int dy);

// The step a bit stands for. Gives (0, 0) for anything that is not one of the
// eight bits above.
void Step(unsigned char bit, int& outDx, int& outDy);
} // namespace Neighbour

// ---------------------------------------------------------------------------
//  The five bands of the sheet, top to bottom.
//
//  THE NUMBERS ARE THE BAND POSITIONS, so a TileSlot can be used directly as a
//  row index into the sheet. That is also Fang's ordering, and the WOLF RPG
//  Editor ordering before it, which is what makes existing art work unchanged.
// ---------------------------------------------------------------------------
enum class TileSlot : unsigned char {
    OuterCorner    = 0,   // convex: nothing beside this corner
    VerticalEdge   = 1,   // a left or right wall: the vertical neighbour only
    HorizontalEdge = 2,   // a top or bottom edge: the horizontal neighbour only
    InnerCorner    = 3,   // concave: both beside it, but the diagonal missing
    Fill           = 4,   // solid interior: all three present
};

inline constexpr int kTileSlotCount = 5;

// The height of a tile sheet, in tiles. Named because it turns up in the
// importer, in the editor's "that sheet is the wrong shape" message and in the
// source-rectangle maths, and a bare 5 in three places is three places to
// forget when one of them changes.
inline constexpr int kSheetBands = kTileSlotCount;

const char* TileSlotName(TileSlot slot);

// ---------------------------------------------------------------------------
//  The four quarters of a tile. The numbers are Fang's part order: top-left is
//  0, and the pairs run across and then down.
// ---------------------------------------------------------------------------
enum class Quadrant : unsigned char {
    TopLeft     = 0,
    TopRight    = 1,
    BottomLeft  = 2,
    BottomRight = 3,
};

inline constexpr int kQuadrantCount = 4;

const char* QuadrantName(Quadrant quadrant);

// ---------------------------------------------------------------------------
//  Which band each quarter comes from: the answer for one tile.
// ---------------------------------------------------------------------------
struct QuadrantSlots {
    // Indexed by Quadrant. A plain array rather than four named members, so it
    // can be looped over - which is what every user of it wants to do.
    std::array<TileSlot, kQuadrantCount> slots{};

    TileSlot operator[](Quadrant quadrant) const {
        return slots[static_cast<std::size_t>(quadrant)];
    }

    friend bool operator==(const QuadrantSlots&, const QuadrantSlots&) = default;
};

// ---------------------------------------------------------------------------
//  THE FUNCTION. Which band does this quarter come from, given the eight
//  neighbour bits?
//
//  INLINE AND constexpr DELIBERATELY, which is a departure from the rest of
//  the engine and worth the two reasons it has. It is three bit tests, asked
//  four times for every visible tile - tens of thousands of times a frame on a
//  large map - and a call across the DLL boundary to do three `&` operations is
//  pure overhead. Being constexpr then lets the pattern table below be built
//  by the same code the renderer runs, so the fast path and its own
//  description cannot drift apart.
// ---------------------------------------------------------------------------
constexpr TileSlot ResolveQuadrant(unsigned char neighbours, Quadrant quadrant) {
    // For each quarter: the straight neighbour above or below it, the straight
    // neighbour beside it, and the diagonal past it.
    //
    // Reading this against the picture at the top of the file is the quickest
    // way to satisfy yourself it is right - the top-left quarter is touched by
    // north, west and north-west, and by nothing else.
    unsigned char vertical   = 0;
    unsigned char horizontal = 0;
    unsigned char diagonal   = 0;
    switch (quadrant) {
        case Quadrant::TopLeft:
            vertical   = Neighbour::kNorth;
            horizontal = Neighbour::kWest;
            diagonal   = Neighbour::kNorthWest;
            break;
        case Quadrant::TopRight:
            vertical   = Neighbour::kNorth;
            horizontal = Neighbour::kEast;
            diagonal   = Neighbour::kNorthEast;
            break;
        case Quadrant::BottomLeft:
            vertical   = Neighbour::kSouth;
            horizontal = Neighbour::kWest;
            diagonal   = Neighbour::kSouthWest;
            break;
        case Quadrant::BottomRight:
            vertical   = Neighbour::kSouth;
            horizontal = Neighbour::kEast;
            diagonal   = Neighbour::kSouthEast;
            break;
    }

    const bool hasVertical   = (neighbours & vertical) != 0;
    const bool hasHorizontal = (neighbours & horizontal) != 0;
    const bool hasDiagonal   = (neighbours & diagonal) != 0;

    // The five cases, and they are exhaustive: two booleans give four
    // branches, and the last of those splits on the diagonal. There is no
    // "otherwise" here to get wrong.
    if (!hasVertical && !hasHorizontal) { return TileSlot::OuterCorner; }
    if (hasVertical && !hasHorizontal)  { return TileSlot::VerticalEdge; }
    if (!hasVertical && hasHorizontal)  { return TileSlot::HorizontalEdge; }
    return hasDiagonal ? TileSlot::Fill : TileSlot::InnerCorner;
}

// All four quarters at once, which is what a renderer actually asks for.
constexpr QuadrantSlots ResolveQuadrants(unsigned char neighbours) {
    QuadrantSlots result{};
    for (int i = 0; i < kQuadrantCount; ++i) {
        result.slots[static_cast<std::size_t>(i)] =
            ResolveQuadrant(neighbours, static_cast<Quadrant>(i));
    }
    return result;
}

// ---------------------------------------------------------------------------
//  WHERE THE PIXELS ARE: four numbers into the sheet, and the reason nothing
//  needs baking.
//
//  `frame` picks a column, for animation or random variation; 0 for a plain
//  sheet. `tileSize` is the size of a whole tile in the sheet, so the returned
//  rectangle is half that in each direction.
//
//  THE Y AXIS TURNS OVER HERE, AND ONLY HERE. A SpriteRect counts down from
//  the top of the image, so the BOTTOM quarters of a tile are at the LARGER y
//  - the opposite of the grid, where north is +y. Flipping it somewhere else
//  as well would cancel out and leave tiles that look almost right.
// ---------------------------------------------------------------------------
SpriteRect QuadrantSourceRect(TileSlot slot, Quadrant quadrant, int tileSize,
                              int frame = 0);

// ---------------------------------------------------------------------------
//  The 47 patterns, enumerated. This is Fang's FangAutoTilePattern, kept
//  because it is the system's own description of itself.
//
//  NOTHING IN THE RENDERER USES IT. ResolveQuadrant above answers the same
//  question in three bit tests with no table to search. The table is here for
//  three other reasons, each of which earns it:
//
//    1. IT IS THE INDEPENDENT CHECK. The tests resolve all 256 neighbour masks
//       both ways and require the two answers to agree, so a mistake in the
//       fast path cannot pass unnoticed.
//    2. `mask` and `combination` are exactly Fang's, so a pattern here can be
//       compared against the original implementation number for number.
//    3. Anyone wanting to bake a 47-tile atlas - to hand to a mesh, or to
//       export for another tool - needs the list, and should not have to
//       derive it a second time.
// ---------------------------------------------------------------------------
struct AutoTilePattern {
    QuadrantSlots quadrants{};

    // Which neighbour bits this pattern cares about, and what they have to be.
    // It matches when `neighbours & mask == combination`.
    //
    // The two together are how "the diagonal does not matter here" is said in
    // a single comparison: a bit absent from `mask` is a bit not looked at.
    unsigned char mask        = 0;
    unsigned char combination = 0;

    bool Matches(unsigned char neighbours) const {
        return static_cast<unsigned char>(neighbours & mask) == combination;
    }
};

// Exactly 47 - and the tests say so, rather than taking the README's word.
inline constexpr int kAutoTilePatternCount = 47;

const std::array<AutoTilePattern, kAutoTilePatternCount>& AutoTilePatterns();

// Which pattern a neighbour mask selects. Always 0..46: every one of the 256
// masks matches exactly one pattern, which is the property the tests pin down.
int FindPatternIndex(unsigned char neighbours);

} // namespace eng
