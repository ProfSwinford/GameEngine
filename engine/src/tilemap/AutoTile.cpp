#include <engine/tilemap/AutoTile.h>

#include <vector>

namespace eng {

// ---------------------------------------------------------------------------
//  Neighbour helpers
// ---------------------------------------------------------------------------

unsigned char Neighbour::Bit(int dx, int dy) {
    // Written out rather than computed, because the bit numbering is a
    // convention inherited from Fang rather than anything derivable. A clever
    // formula here would be a second description of the same table, and the
    // two would eventually disagree.
    if (dx == 0 && dy == 1)   { return kNorth; }
    if (dx == 1 && dy == 1)   { return kNorthEast; }
    if (dx == 1 && dy == 0)   { return kEast; }
    if (dx == 1 && dy == -1)  { return kSouthEast; }
    if (dx == 0 && dy == -1)  { return kSouth; }
    if (dx == -1 && dy == -1) { return kSouthWest; }
    if (dx == -1 && dy == 0)  { return kWest; }
    if (dx == -1 && dy == 1)  { return kNorthWest; }
    return 0;   // includes (0, 0): a tile is not its own neighbour
}

void Neighbour::Step(unsigned char bit, int& outDx, int& outDy) {
    switch (bit) {
        case kNorth:     outDx = 0;  outDy = 1;  return;
        case kNorthEast: outDx = 1;  outDy = 1;  return;
        case kEast:      outDx = 1;  outDy = 0;  return;
        case kSouthEast: outDx = 1;  outDy = -1; return;
        case kSouth:     outDx = 0;  outDy = -1; return;
        case kSouthWest: outDx = -1; outDy = -1; return;
        case kWest:      outDx = -1; outDy = 0;  return;
        case kNorthWest: outDx = -1; outDy = 1;  return;
        default:         outDx = 0;  outDy = 0;  return;
    }
}

const char* TileSlotName(TileSlot slot) {
    switch (slot) {
        case TileSlot::OuterCorner:    return "OuterCorner";
        case TileSlot::VerticalEdge:   return "VerticalEdge";
        case TileSlot::HorizontalEdge: return "HorizontalEdge";
        case TileSlot::InnerCorner:    return "InnerCorner";
        case TileSlot::Fill:           return "Fill";
    }
    return "?";
}

const char* QuadrantName(Quadrant quadrant) {
    switch (quadrant) {
        case Quadrant::TopLeft:     return "TopLeft";
        case Quadrant::TopRight:    return "TopRight";
        case Quadrant::BottomLeft:  return "BottomLeft";
        case Quadrant::BottomRight: return "BottomRight";
    }
    return "?";
}

// ---------------------------------------------------------------------------
//  The sheet lookup
// ---------------------------------------------------------------------------

SpriteRect QuadrantSourceRect(TileSlot slot, Quadrant quadrant, int tileSize,
                              int frame) {
    const int half = tileSize / 2;

    const bool right = (quadrant == Quadrant::TopRight ||
                        quadrant == Quadrant::BottomRight);

    // THE Y FLIP. In the grid, north is +y; in an image, y counts DOWN from
    // the top. So the quarters named "bottom" are the ones at the larger y.
    const bool bottom = (quadrant == Quadrant::BottomLeft ||
                         quadrant == Quadrant::BottomRight);

    SpriteRect rect;
    rect.x = frame * tileSize + (right ? half : 0);
    rect.y = static_cast<int>(slot) * tileSize + (bottom ? half : 0);
    rect.w = half;
    rect.h = half;
    return rect;
}

// ---------------------------------------------------------------------------
//  The 47 patterns
//
//  THE ENUMERATION, and why the filter below is exactly four comparisons.
//
//  Each band implies two facts about the tile it is drawn for: whether the
//  HORIZONTAL boundary beside that corner is closed (there is nothing above or
//  below), and whether the VERTICAL one is:
//
//      OuterCorner      horizontal closed, vertical closed
//      VerticalEdge     horizontal OPEN,   vertical closed
//      HorizontalEdge   horizontal closed, vertical OPEN
//      InnerCorner      horizontal OPEN,   vertical OPEN
//      Fill             horizontal OPEN,   vertical OPEN
//
//  Two bands share the last row, which is the whole reason the count is not a
//  power of two: once both boundaries are open the diagonal decides, and
//  nothing else about the tile does.
//
//  Adjacent quarters read the same neighbour, so they must agree about it. The
//  top pair both read N, the bottom pair both read S, the left pair both read
//  W and the right pair both read E - four constraints, and they are the four
//  `if` statements below.
//
//  COUNTING IT. Those four constraints leave four free booleans: whether the
//  top edge is closed, the bottom edge, the left edge, the right edge. Write s
//  for how many of {left, right} are OPEN and t for how many of {top, bottom}
//  are OPEN; a quarter has both boundaries open - and so a free choice between
//  InnerCorner and Fill - exactly when its own horizontal and vertical are
//  both open, which happens for s*t of the four quarters. So:
//
//      s=0:  1*1*2^0 + 1*2*2^0 + 1*1*2^0                    =  4
//      s=1:  2*1*2^0 + 2*2*2^1 + 2*1*2^2                    = 18
//      s=2:  1*1*2^0 + 1*2*2^2 + 1*1*2^4                    = 25
//                                                             ----
//                                                               47
//
//  which is the number of tiles an artist would otherwise draw by hand. A test
//  asserts the 47 rather than trusting this paragraph.
// ---------------------------------------------------------------------------

namespace {

// Fang's primitive tables, verbatim, including the values in binary so they can
// be read against the bit layout in the header.
//
// A band whose diagonal does not matter masks only the straight bits; the two
// that do care mask all eight.
constexpr unsigned char kSlotMask[kTileSlotCount] = {
    0b01010101,   // OuterCorner     straights only
    0b01010101,   // VerticalEdge    straights only
    0b01010101,   // HorizontalEdge  straights only
    0b11111111,   // InnerCorner     the diagonal decides
    0b11111111,   // Fill            the diagonal decides
};

constexpr unsigned char kSlotCombination[kTileSlotCount] = {
    0b00000000,   // OuterCorner     neither straight present
    0b00010001,   // VerticalEdge    the vertical one present
    0b01000100,   // HorizontalEdge  the horizontal one present
    0b01010101,   // InnerCorner     both straights, diagonal ABSENT
    0b11111111,   // Fill            everything present
};

// Which three bits each quarter is allowed to look at. ANDed with the tables
// above, this is what turns a general "north and south" pattern into "north"
// for the top quarters and "south" for the bottom ones.
constexpr unsigned char kQuadrantWindow[kQuadrantCount] = {
    Neighbour::kNorth | Neighbour::kWest | Neighbour::kNorthWest,   // TL  0b11000001
    Neighbour::kNorth | Neighbour::kEast | Neighbour::kNorthEast,   // TR  0b00000111
    Neighbour::kSouth | Neighbour::kWest | Neighbour::kSouthWest,   // BL  0b01110000
    Neighbour::kSouth | Neighbour::kEast | Neighbour::kSouthEast,   // BR  0b00011100
};

constexpr bool HorizontalEdgeClosed(TileSlot slot) {
    return slot == TileSlot::OuterCorner || slot == TileSlot::HorizontalEdge;
}

constexpr bool VerticalEdgeClosed(TileSlot slot) {
    return slot == TileSlot::OuterCorner || slot == TileSlot::VerticalEdge;
}

std::array<AutoTilePattern, kAutoTilePatternCount> BuildPatterns() {
    std::array<AutoTilePattern, kAutoTilePatternCount> patterns{};
    std::size_t written = 0;

    for (int tl = 0; tl < kTileSlotCount; ++tl) {
        const auto slotTL = static_cast<TileSlot>(tl);
        for (int tr = 0; tr < kTileSlotCount; ++tr) {
            const auto slotTR = static_cast<TileSlot>(tr);
            // The top pair both read N.
            if (HorizontalEdgeClosed(slotTL) != HorizontalEdgeClosed(slotTR)) {
                continue;
            }
            for (int bl = 0; bl < kTileSlotCount; ++bl) {
                const auto slotBL = static_cast<TileSlot>(bl);
                // The left pair both read W.
                if (VerticalEdgeClosed(slotTL) != VerticalEdgeClosed(slotBL)) {
                    continue;
                }
                for (int br = 0; br < kTileSlotCount; ++br) {
                    const auto slotBR = static_cast<TileSlot>(br);
                    // The bottom pair both read S, the right pair both read E.
                    if (HorizontalEdgeClosed(slotBL) != HorizontalEdgeClosed(slotBR)) {
                        continue;
                    }
                    if (VerticalEdgeClosed(slotTR) != VerticalEdgeClosed(slotBR)) {
                        continue;
                    }

                    AutoTilePattern pattern{};
                    pattern.quadrants.slots = {slotTL, slotTR, slotBL, slotBR};

                    for (int q = 0; q < kQuadrantCount; ++q) {
                        const auto index =
                            static_cast<std::size_t>(pattern.quadrants.slots[
                                static_cast<std::size_t>(q)]);
                        const unsigned char window =
                            kQuadrantWindow[static_cast<std::size_t>(q)];
                        pattern.mask = static_cast<unsigned char>(
                            pattern.mask | (kSlotMask[index] & window));
                        pattern.combination = static_cast<unsigned char>(
                            pattern.combination | (kSlotCombination[index] & window));
                    }

                    // If this ever fires, the filter above and the 47 in the
                    // header have come apart. Clamping and carrying on would
                    // hide it, so the write is simply refused and the count
                    // check below reports it.
                    if (written < patterns.size()) {
                        patterns[written] = pattern;
                    }
                    ++written;
                }
            }
        }
    }

    // Deliberately not an assert: a release build would drop it, and this is
    // the one fact the whole file rests on. The array is sized 47, so a
    // different count leaves default-constructed entries that the tests catch
    // immediately - every mask would resolve to pattern 0.
    if (written != patterns.size()) {
        patterns.fill(AutoTilePattern{});
    }
    return patterns;
}

} // namespace

const std::array<AutoTilePattern, kAutoTilePatternCount>& AutoTilePatterns() {
    // A function-local static, not a file-level one. The engine is a DLL that
    // exports functions and NOT data (see engine/CMakeLists.txt), so state
    // lives inside a function and is reached by calling it.
    static const std::array<AutoTilePattern, kAutoTilePatternCount> patterns =
        BuildPatterns();
    return patterns;
}

int FindPatternIndex(unsigned char neighbours) {
    const auto& patterns = AutoTilePatterns();
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        if (patterns[i].Matches(neighbours)) {
            return static_cast<int>(i);
        }
    }

    // Unreachable: the enumeration covers every one of the 256 masks, which a
    // test checks directly. Returning 0 rather than -1 keeps a caller that
    // ignores the possibility from indexing out of bounds.
    return 0;
}

} // namespace eng
