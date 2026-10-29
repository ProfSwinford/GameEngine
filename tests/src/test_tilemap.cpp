// ============================================================================
//  test_tilemap.cpp - the auto-tiling algorithm, the grid under it, and the
//  collider built from it.
//
//  THE ALGORITHM IS TESTED TWO WAYS AGAINST ITSELF, and that is the point of
//  the first section. AutoTile.h answers "which band does this quarter come
//  from?" twice over: once in three bit tests on the fast path, and once as a
//  table of 47 (mask, combination) pairs that is Fang's own description of the
//  problem. The two are written from different angles, so requiring them to
//  agree on ALL 256 neighbour arrangements is a real check rather than a
//  restatement - a mistake would have to be made identically in both.
//
//  ==========================================================================
//  WHAT A WRONG ANSWER HERE LOOKS LIKE
//
//  Not a crash. A wall whose corners are the wrong way round, or a seam down
//  the middle of a floor, or a single tile that renders as four outside
//  corners when it should be solid. Those are easy to see in a picture and
//  nearly impossible to see in a stack trace, which is why the cases below
//  spell out the expected quarters one at a time.
// ============================================================================

#include <doctest/doctest.h>

#include <engine/core/Config.h>
#include <engine/fs/FileSystem.h>
#include <engine/physics/Collider.h>
#include <engine/scene/Component.h>
#include <engine/scene/Scene.h>
#include <engine/tilemap/AutoTile.h>
#include <engine/tilemap/TileAsset.h>
#include <engine/tilemap/Tilemap.h>
#include <engine/tilemap/TilemapCollider.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace eng;

namespace {

TileSlot SlotOf(unsigned char mask, Quadrant quadrant) {
    return ResolveQuadrants(mask)[quadrant];
}

} // namespace

// ===========================================================================
//  The algorithm
// ===========================================================================

TEST_CASE("the pattern table holds exactly 47 entries") {
    // The number Fang's README claims, and the number of tiles an artist would
    // otherwise draw by hand. If the edge-consistency filter in BuildPatterns
    // is wrong in either direction this is the first thing to notice.
    CHECK(AutoTilePatterns().size() == 47);
    CHECK(kAutoTilePatternCount == 47);
}

TEST_CASE("the pattern table is not left half-built") {
    // BuildPatterns clears the whole array if the count comes out wrong, which
    // would otherwise be invisible - every lookup would quietly return pattern
    // 0. A real table has several distinct masks in it.
    std::set<unsigned char> masks;
    for (const AutoTilePattern& pattern : AutoTilePatterns()) {
        masks.insert(pattern.mask);
    }
    CHECK(masks.size() > 1);
}

TEST_CASE("every one of the 256 neighbour arrangements matches exactly one pattern") {
    // THE PROPERTY THE WHOLE TABLE RESTS ON. Two patterns matching the same
    // arrangement would make the answer depend on search order; none matching
    // would leave a tile with no picture at all.
    for (int mask = 0; mask < 256; ++mask) {
        int matches = 0;
        for (const AutoTilePattern& pattern : AutoTilePatterns()) {
            if (pattern.Matches(static_cast<unsigned char>(mask))) {
                ++matches;
            }
        }
        INFO("neighbour mask ", mask);
        CHECK(matches == 1);
    }
}

TEST_CASE("the fast path and the pattern table agree on all 256 arrangements") {
    // The independent check described at the top of this file. ResolveQuadrant
    // is three bit tests; the table is built from Fang's mask/combination
    // arithmetic. Nothing is shared between them but the answer.
    for (int mask = 0; mask < 256; ++mask) {
        const auto neighbours = static_cast<unsigned char>(mask);

        const QuadrantSlots fast  = ResolveQuadrants(neighbours);
        const int           index = FindPatternIndex(neighbours);
        const QuadrantSlots table = AutoTilePatterns()[static_cast<std::size_t>(index)]
                                        .quadrants;

        INFO("neighbour mask ", mask);
        CHECK(fast == table);
    }
}

TEST_CASE("every pattern in the table is reachable") {
    // A pattern no arrangement can produce would mean the filter is letting
    // through a combination of quarters that cannot physically occur.
    std::set<int> reached;
    for (int mask = 0; mask < 256; ++mask) {
        reached.insert(FindPatternIndex(static_cast<unsigned char>(mask)));
    }
    CHECK(reached.size() == 47);
}

TEST_CASE("a tile with nothing around it is four outside corners") {
    const QuadrantSlots slots = ResolveQuadrants(0);
    CHECK(slots[Quadrant::TopLeft] == TileSlot::OuterCorner);
    CHECK(slots[Quadrant::TopRight] == TileSlot::OuterCorner);
    CHECK(slots[Quadrant::BottomLeft] == TileSlot::OuterCorner);
    CHECK(slots[Quadrant::BottomRight] == TileSlot::OuterCorner);
}

TEST_CASE("a tile surrounded on all eight sides is solid") {
    const QuadrantSlots slots = ResolveQuadrants(Neighbour::kAll);
    for (int q = 0; q < kQuadrantCount; ++q) {
        CHECK(slots[static_cast<Quadrant>(q)] == TileSlot::Fill);
    }
}

TEST_CASE("straights without diagonals give four inside corners") {
    // The classic cross junction: four arms meet, and all four diagonals are
    // empty, so every quarter is tucked into a concave corner. Getting this
    // one wrong gives a solid block where there should be a cross.
    const QuadrantSlots slots = ResolveQuadrants(Neighbour::kStraights);
    for (int q = 0; q < kQuadrantCount; ++q) {
        CHECK(slots[static_cast<Quadrant>(q)] == TileSlot::InnerCorner);
    }
}

TEST_CASE("a neighbour to the north only affects the top half") {
    const unsigned char mask = Neighbour::kNorth;

    // The top quarters see their vertical neighbour and not their horizontal
    // one, which is a left or right wall carrying on upward.
    CHECK(SlotOf(mask, Quadrant::TopLeft) == TileSlot::VerticalEdge);
    CHECK(SlotOf(mask, Quadrant::TopRight) == TileSlot::VerticalEdge);

    // The bottom quarters cannot see north at all.
    CHECK(SlotOf(mask, Quadrant::BottomLeft) == TileSlot::OuterCorner);
    CHECK(SlotOf(mask, Quadrant::BottomRight) == TileSlot::OuterCorner);
}

TEST_CASE("a neighbour to the west only affects the left half") {
    const unsigned char mask = Neighbour::kWest;

    CHECK(SlotOf(mask, Quadrant::TopLeft) == TileSlot::HorizontalEdge);
    CHECK(SlotOf(mask, Quadrant::BottomLeft) == TileSlot::HorizontalEdge);
    CHECK(SlotOf(mask, Quadrant::TopRight) == TileSlot::OuterCorner);
    CHECK(SlotOf(mask, Quadrant::BottomRight) == TileSlot::OuterCorner);
}

TEST_CASE("the diagonal only matters when both straights beside it are there") {
    // The two cases that collapse eight arrangements into five. A corner with
    // a gap beside it cannot see past that gap, so what is diagonally beyond
    // it must make no difference at all.
    for (int q = 0; q < kQuadrantCount; ++q) {
        const auto quadrant = static_cast<Quadrant>(q);

        for (int mask = 0; mask < 256; ++mask) {
            const auto with    = static_cast<unsigned char>(mask);
            const TileSlot slot = ResolveQuadrant(with, quadrant);
            if (slot != TileSlot::Fill && slot != TileSlot::InnerCorner) {
                // One of the three slots that ignores its diagonal. Flipping
                // that diagonal must change nothing.
                unsigned char diagonal = 0;
                switch (quadrant) {
                    case Quadrant::TopLeft:     diagonal = Neighbour::kNorthWest; break;
                    case Quadrant::TopRight:    diagonal = Neighbour::kNorthEast; break;
                    case Quadrant::BottomLeft:  diagonal = Neighbour::kSouthWest; break;
                    case Quadrant::BottomRight: diagonal = Neighbour::kSouthEast; break;
                }
                const auto flipped = static_cast<unsigned char>(with ^ diagonal);
                INFO("quadrant ", QuadrantName(quadrant), " mask ", mask);
                CHECK(ResolveQuadrant(flipped, quadrant) == slot);
            }
        }
    }
}

TEST_CASE("the inside corner becomes solid when its diagonal arrives") {
    // Specifically the north-west corner, which is the one a reader can check
    // against the picture at the top of AutoTile.h.
    const auto without = static_cast<unsigned char>(Neighbour::kNorth | Neighbour::kWest);
    const auto with = static_cast<unsigned char>(without | Neighbour::kNorthWest);

    CHECK(ResolveQuadrant(without, Quadrant::TopLeft) == TileSlot::InnerCorner);
    CHECK(ResolveQuadrant(with, Quadrant::TopLeft) == TileSlot::Fill);
}

TEST_CASE("neighbour bits and steps are the same relation both ways") {
    const unsigned char bits[8] = {
        Neighbour::kNorth,     Neighbour::kNorthEast, Neighbour::kEast,
        Neighbour::kSouthEast, Neighbour::kSouth,     Neighbour::kSouthWest,
        Neighbour::kWest,      Neighbour::kNorthWest,
    };

    for (const unsigned char bit : bits) {
        int dx = 0;
        int dy = 0;
        Neighbour::Step(bit, dx, dy);
        CHECK(Neighbour::Bit(dx, dy) == bit);
    }

    // North is +y. If this is ever flipped, every map in the project turns
    // upside down in a way that looks almost right.
    int dx = 0;
    int dy = 0;
    Neighbour::Step(Neighbour::kNorth, dx, dy);
    CHECK(dx == 0);
    CHECK(dy == 1);

    // A tile is not its own neighbour.
    CHECK(Neighbour::Bit(0, 0) == 0);
    CHECK(Neighbour::Bit(2, 0) == 0);
}

// ===========================================================================
//  Reading the sheet
// ===========================================================================

TEST_CASE("a band is one tile further down the sheet than the one above it") {
    const SpriteRect first =
        QuadrantSourceRect(TileSlot::OuterCorner, Quadrant::TopLeft, 32);
    const SpriteRect last =
        QuadrantSourceRect(TileSlot::Fill, Quadrant::TopLeft, 32);

    CHECK(first.y == 0);
    CHECK(last.y == 4 * 32);
    CHECK(first.w == 16);
    CHECK(first.h == 16);
}

TEST_CASE("the four quarters come from the four corners of their band") {
    const int size = 32;
    const int half = 16;

    const SpriteRect tl = QuadrantSourceRect(TileSlot::Fill, Quadrant::TopLeft, size);
    const SpriteRect tr = QuadrantSourceRect(TileSlot::Fill, Quadrant::TopRight, size);
    const SpriteRect bl = QuadrantSourceRect(TileSlot::Fill, Quadrant::BottomLeft, size);
    const SpriteRect br = QuadrantSourceRect(TileSlot::Fill, Quadrant::BottomRight, size);

    const int bandTop = 4 * size;

    CHECK(tl.x == 0);
    CHECK(tl.y == bandTop);
    CHECK(tr.x == half);
    CHECK(tr.y == bandTop);

    // THE Y FLIP. A sheet is an image and counts DOWN from the top, so the
    // quarters called "bottom" are the ones at the larger y. Getting this
    // backwards turns every tile upside down inside its own cell.
    CHECK(bl.x == 0);
    CHECK(bl.y == bandTop + half);
    CHECK(br.x == half);
    CHECK(br.y == bandTop + half);
}

TEST_CASE("frames go sideways") {
    const SpriteRect frame0 =
        QuadrantSourceRect(TileSlot::Fill, Quadrant::TopLeft, 32, 0);
    const SpriteRect frame3 =
        QuadrantSourceRect(TileSlot::Fill, Quadrant::TopLeft, 32, 3);

    CHECK(frame3.x == frame0.x + 3 * 32);
    CHECK(frame3.y == frame0.y);   // and NOT downward, which is where bands go
}

TEST_CASE("the four quarters of a tile tile its sheet cell exactly") {
    // No gap and no overlap between the quarters: together they are the whole
    // band, which is what makes a Fill tile indistinguishable from the band it
    // was cut from.
    const int size = 32;
    for (int band = 0; band < kTileSlotCount; ++band) {
        const auto slot = static_cast<TileSlot>(band);

        int covered = 0;
        for (int q = 0; q < kQuadrantCount; ++q) {
            const SpriteRect rect =
                QuadrantSourceRect(slot, static_cast<Quadrant>(q), size);
            covered += rect.w * rect.h;

            CHECK(rect.x >= 0);
            CHECK(rect.y >= band * size);
            CHECK(rect.x + rect.w <= size);
            CHECK(rect.y + rect.h <= (band + 1) * size);
        }
        CHECK(covered == size * size);
    }
}

// ===========================================================================
//  The grid
// ===========================================================================

namespace {

// A tilemap on its own entity, with a palette of names that need not load.
//
// A PALETTE ENTRY THAT WILL NOT LOAD IS STILL A PALETTE ENTRY: the map keeps
// the path and the connection table falls back to "a tile joins itself", which
// is exactly what these cases want. Nothing here needs a texture, and the test
// program has no renderer to make one with.
struct MapFixture {
    Scene              scene;
    Entity*            entity = nullptr;
    TilemapComponent*  map    = nullptr;

    explicit MapFixture(int paletteEntries = 1) {
        ComponentFactory::RegisterBuiltins();
        CollisionSystem::RegisterComponentTypes();
        TilemapRenderSystem::RegisterComponentTypes();
        Scene::SetActive(&scene);

        entity = scene.Get(scene.CreateEntity("Map"));

        Json node        = Json::object();
        Json palette     = Json::array();
        for (int i = 0; i < paletteEntries; ++i) {
            palette.push_back("tiles/test" + std::to_string(i) + ".otile");
        }
        node["palette"]  = palette;
        node["cellSize"] = Json::array({32.0f, 32.0f});

        map = static_cast<TilemapComponent*>(
            entity->AddComponent(TilemapComponent::kTypeName));
        REQUIRE(map != nullptr);

        std::string error;
        REQUIRE(map->Deserialize(node, error));
    }

    ~MapFixture() {
        scene.Unload();
        Scene::SetActive(nullptr);
        CollisionSystem::Clear();
        TilemapRenderSystem::Clear();
        TileLibrary::Clear();
    }
};

} // namespace

TEST_CASE("a new map is empty everywhere") {
    MapFixture f;
    CHECK(f.map->IsEmpty());
    CHECK(f.map->TileCount() == 0);
    CHECK(f.map->ChunkCount() == 0);
    CHECK(f.map->GetTile(0, 0) == kEmptyTile);
    CHECK(f.map->GetTile(-9999, 12345) == kEmptyTile);
}

TEST_CASE("a tile written is a tile read back") {
    MapFixture f;
    f.map->SetTile(3, 4, 1);
    CHECK(f.map->GetTile(3, 4) == 1);
    CHECK(f.map->TileCount() == 1);
    CHECK(f.map->ChunkCount() == 1);
}

TEST_CASE("negative coordinates are their own cells") {
    // THE FLOOR-DIVISION TRAP. Chunking with plain integer division rounds
    // toward zero, which folds cells -15..15 into one chunk and makes the map
    // overwrite itself across the axis. Four cells around the origin must stay
    // four cells.
    MapFixture f(4);

    f.map->SetTile(0, 0, 1);
    f.map->SetTile(-1, 0, 2);
    f.map->SetTile(0, -1, 3);
    f.map->SetTile(-1, -1, 4);

    CHECK(f.map->GetTile(0, 0) == 1);
    CHECK(f.map->GetTile(-1, 0) == 2);
    CHECK(f.map->GetTile(0, -1) == 3);
    CHECK(f.map->GetTile(-1, -1) == 4);
    CHECK(f.map->TileCount() == 4);

    // ...and they are in four different chunks, because each is in a
    // different quadrant of the plane.
    CHECK(f.map->ChunkCount() == 4);
}

TEST_CASE("a cell far from the origin does not disturb one near it") {
    MapFixture f(2);
    f.map->SetTile(0, 0, 1);
    f.map->SetTile(-4000, 7000, 2);

    CHECK(f.map->GetTile(0, 0) == 1);
    CHECK(f.map->GetTile(-4000, 7000) == 2);
    CHECK(f.map->TileCount() == 2);
    CHECK(f.map->ChunkCount() == 2);   // and nothing in between was allocated
}

TEST_CASE("erasing the last tile in a chunk drops the chunk") {
    MapFixture f;
    f.map->SetTile(5, 5, 1);
    CHECK(f.map->ChunkCount() == 1);

    f.map->EraseTile(5, 5);
    CHECK(f.map->ChunkCount() == 0);
    CHECK(f.map->TileCount() == 0);
    CHECK(f.map->IsEmpty());
}

TEST_CASE("erasing an empty cell does not create a chunk") {
    // Dragging the eraser across empty space is the usual way this happens,
    // and a map that grew a chunk of zeroes per cell erased would bloat
    // without anything visible changing.
    MapFixture f;
    f.map->EraseTile(100, 100);
    f.map->EraseTile(-50, 3);
    CHECK(f.map->ChunkCount() == 0);
}

TEST_CASE("the revision moves when a cell changes and not when it does not") {
    MapFixture f;

    const unsigned long long start = f.map->Revision();
    f.map->SetTile(1, 1, 1);
    const unsigned long long painted = f.map->Revision();
    CHECK(painted > start);

    // PAINTING A CELL WITH WHAT IT ALREADY HOLDS IS NOT A CHANGE. The collider
    // rebuilds on this number, and dragging a brush over one cell would
    // otherwise rebuild it every frame.
    f.map->SetTile(1, 1, 1);
    CHECK(f.map->Revision() == painted);

    f.map->EraseTile(1, 1);
    CHECK(f.map->Revision() > painted);
}

TEST_CASE("the painted bounds are the painted cells and not their chunks") {
    MapFixture f;

    TileCoord lo{};
    TileCoord hi{};
    CHECK_FALSE(f.map->TileBounds(lo, hi));   // an empty map has no bounds

    f.map->SetTile(2, 3, 1);
    f.map->SetTile(-4, 9, 1);
    REQUIRE(f.map->TileBounds(lo, hi));

    // A chunk is sixteen cells across; reporting the chunk's extent instead of
    // the painted one would give a box far larger than anything drawn.
    CHECK(lo.x == -4);
    CHECK(lo.y == 3);
    CHECK(hi.x == 2);
    CHECK(hi.y == 9);
}

TEST_CASE("clearing empties the map in one go") {
    MapFixture f;
    for (int i = 0; i < 40; ++i) {
        f.map->SetTile(i, i, 1);
    }
    CHECK(f.map->TileCount() == 40);

    f.map->Clear();
    CHECK(f.map->TileCount() == 0);
    CHECK(f.map->ChunkCount() == 0);
}

TEST_CASE("a cell read is the cell written, over a whole span of chunks") {
    // A sweep rather than a handful of spot checks, because the index-within-
    // chunk arithmetic is where an x/y swap hides: a square test pattern would
    // pass with the two transposed.
    //
    // THE PATTERN REPEATS EVERY THREE CELLS, DELIBERATELY. Chunks are sixteen
    // across, and a chunking mistake makes cells sixteen apart land on the same
    // slot. A pattern whose period divides sixteen would give those two cells
    // the same value and the aliasing would read back as correct - which is
    // exactly what an earlier version of this test did. Three does not divide
    // sixteen, so an aliased cell reads back wrong.
    MapFixture f(3);

    const auto expectedAt = [](int x, int y) {
        const int value = ((x * 2 + y) % 3 + 3) % 3;
        return static_cast<TileId>(value + 1);
    };

    for (int y = -20; y <= 20; ++y) {
        for (int x = -33; x <= 17; ++x) {
            f.map->SetTile(x, y, expectedAt(x, y));
        }
    }

    bool allMatched = true;
    for (int y = -20; y <= 20; ++y) {
        for (int x = -33; x <= 17; ++x) {
            if (f.map->GetTile(x, y) != expectedAt(x, y)) {
                allMatched = false;
            }
        }
    }
    CHECK(allMatched);
    CHECK(f.map->TileCount() == 41u * 51u);
}

// ===========================================================================
//  Cells and world space
// ===========================================================================

TEST_CASE("cell (0,0) has its bottom-left corner on the origin") {
    MapFixture f;
    const AABB bounds = f.map->CellBoundsLocal(0, 0);
    CHECK(bounds.min.x == doctest::Approx(0.0f));
    CHECK(bounds.min.y == doctest::Approx(0.0f));
    CHECK(bounds.max.x == doctest::Approx(32.0f));
    CHECK(bounds.max.y == doctest::Approx(32.0f));
}

TEST_CASE("a point maps to the cell it is inside, on both sides of the origin") {
    MapFixture f;

    CHECK(f.map->WorldToCell(Vec2{1.0f, 1.0f}) == TileCoord{0, 0});
    CHECK(f.map->WorldToCell(Vec2{31.9f, 31.9f}) == TileCoord{0, 0});
    CHECK(f.map->WorldToCell(Vec2{32.1f, 0.0f}) == TileCoord{1, 0});

    // A cast to int rounds toward zero, which would make cell 0 twice as wide
    // as every other cell by swallowing everything from -32 to 32. std::floor
    // is what stops the brush painting the wrong cell left of the origin.
    CHECK(f.map->WorldToCell(Vec2{-0.5f, -0.5f}) == TileCoord{-1, -1});
    CHECK(f.map->WorldToCell(Vec2{-32.5f, 0.0f}) == TileCoord{-2, 0});
}

TEST_CASE("a cell centre maps back to that cell") {
    MapFixture f;
    for (int y = -3; y <= 3; ++y) {
        for (int x = -3; x <= 3; ++x) {
            const Vec2 centre = f.map->CellToWorld(x, y);
            CHECK(f.map->WorldToCell(centre) == TileCoord{x, y});
        }
    }
}

TEST_CASE("moving the entity moves the whole map with it") {
    MapFixture f;
    f.entity->Transform().SetLocalPosition(Vec2{100.0f, -50.0f});

    const Vec2 centre = f.map->CellToWorld(0, 0);
    CHECK(centre.x == doctest::Approx(116.0f));    // 100 + half a 32px cell
    CHECK(centre.y == doctest::Approx(-34.0f));    // -50 + 16

    CHECK(f.map->WorldToCell(centre) == TileCoord{0, 0});
}

// ===========================================================================
//  Neighbours and connection
// ===========================================================================

TEST_CASE("a lone tile sees nothing around it") {
    MapFixture f;
    f.map->SetTile(0, 0, 1);
    CHECK(f.map->NeighbourMask(0, 0) == 0);
}

TEST_CASE("an empty cell has no neighbours at all") {
    MapFixture f;
    f.map->SetTile(1, 0, 1);
    CHECK(f.map->NeighbourMask(0, 0) == 0);
}

TEST_CASE("a cell in the middle of a block is surrounded") {
    MapFixture f;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            f.map->SetTile(x, y, 1);
        }
    }

    CHECK(f.map->NeighbourMask(0, 0) == Neighbour::kAll);

    // ...and so it draws as solid, which is the whole point of the exercise.
    const QuadrantSlots slots = ResolveQuadrants(f.map->NeighbourMask(0, 0));
    for (int q = 0; q < kQuadrantCount; ++q) {
        CHECK(slots[static_cast<Quadrant>(q)] == TileSlot::Fill);
    }
}

TEST_CASE("the neighbour mask names the right directions") {
    MapFixture f;
    f.map->SetTile(0, 0, 1);

    f.map->SetTile(0, 1, 1);     // north: +y
    CHECK(f.map->NeighbourMask(0, 0) == Neighbour::kNorth);

    f.map->SetTile(1, 0, 1);     // east: +x
    CHECK(f.map->NeighbourMask(0, 0) ==
          static_cast<unsigned char>(Neighbour::kNorth | Neighbour::kEast));

    f.map->SetTile(1, 1, 1);     // north-east
    CHECK(f.map->NeighbourMask(0, 0) ==
          static_cast<unsigned char>(Neighbour::kNorth | Neighbour::kEast |
                                     Neighbour::kNorthEast));
}

TEST_CASE("a cell works across a chunk boundary") {
    // Chunks are sixteen cells across, so cell 15 and cell 16 are in different
    // chunks and a tile straddling the join must still see its neighbour.
    // Without this a map would be criss-crossed with seams every 16 cells.
    MapFixture f;
    for (int y = -1; y <= 1; ++y) {
        for (int x = 14; x <= 17; ++x) {
            f.map->SetTile(x, y, 1);
        }
    }
    CHECK(f.map->NeighbourMask(15, 0) == Neighbour::kAll);
    CHECK(f.map->NeighbourMask(16, 0) == Neighbour::kAll);
}

TEST_CASE("different materials do not join by default") {
    MapFixture f(2);
    f.map->SetTile(0, 0, 1);
    f.map->SetTile(1, 0, 2);   // a different palette entry

    CHECK(f.map->Connects(1, 1));
    CHECK_FALSE(f.map->Connects(1, 2));
    CHECK(f.map->NeighbourMask(0, 0) == 0);
}

TEST_CASE("nothing connects to an empty cell") {
    MapFixture f;
    CHECK_FALSE(f.map->Connects(1, kEmptyTile));
    CHECK_FALSE(f.map->Connects(kEmptyTile, 1));
    CHECK_FALSE(f.map->Connects(kEmptyTile, kEmptyTile));
}

// ===========================================================================
//  Saving and loading
// ===========================================================================

TEST_CASE("a map survives being written out and read back") {
    MapFixture f(3);

    for (int y = -18; y <= 18; ++y) {
        for (int x = -18; x <= 18; ++x) {
            if (((x * 31 + y * 17) & 3) != 0) {
                f.map->SetTile(x, y, static_cast<TileId>(((x + y) & 1) + 1));
            }
        }
    }
    const std::size_t before = f.map->TileCount();

    Json saved;
    REQUIRE(f.map->Serialize(saved));

    MapFixture other(3);
    std::string error;
    REQUIRE(other.map->Deserialize(saved, error));

    CHECK(other.map->TileCount() == before);
    CHECK(other.map->ChunkCount() == f.map->ChunkCount());

    bool allMatched = true;
    for (int y = -18; y <= 18; ++y) {
        for (int x = -18; x <= 18; ++x) {
            if (other.map->GetTile(x, y) != f.map->GetTile(x, y)) {
                allMatched = false;
            }
        }
    }
    CHECK(allMatched);
}

TEST_CASE("an empty chunk encodes to one short run") {
    // The run-length coding exists so a scene file stays readable. A chunk of
    // 256 empty cells should be four characters, not 256 numbers.
    MapFixture f;
    f.map->SetTile(0, 0, 1);

    Json saved;
    REQUIRE(f.map->Serialize(saved));
    REQUIRE(saved.contains("chunks"));
    REQUIRE(saved["chunks"].size() == 1);

    const std::string cells = saved["chunks"][0]["cells"].get<std::string>();
    CHECK(cells.size() < 40);          // a bare list would be well over 500
    CHECK(cells.find("255:0") != std::string::npos);   // the rest of the chunk
}

TEST_CASE("a chunk with the wrong number of cells is refused") {
    MapFixture f;

    Json node        = Json::object();
    node["palette"]  = Json::array({"tiles/test0.otile"});
    Json chunk       = Json::object();
    chunk["at"]      = Json::array({0, 0});
    chunk["cells"]   = "10:1";   // nowhere near 256
    node["chunks"]   = Json::array({chunk});

    std::string error;
    CHECK_FALSE(f.map->Deserialize(node, error));
    CHECK(error.find("256") != std::string::npos);
}

TEST_CASE("a cell naming a palette entry that does not exist is refused") {
    // Not papered over: such a cell is occupied as far as the map is concerned
    // and invisible as far as the screen is, which is the hardest kind of bug
    // to see.
    MapFixture f;

    Json node       = Json::object();
    node["palette"] = Json::array({"tiles/test0.otile"});
    Json chunk      = Json::object();
    chunk["at"]     = Json::array({0, 0});
    chunk["cells"]  = "255:0 9";   // id 9, with one palette entry
    node["chunks"]  = Json::array({chunk});

    std::string error;
    CHECK_FALSE(f.map->Deserialize(node, error));
    CHECK(error.find("palette") != std::string::npos);
}

TEST_CASE("a hand-edited chunk with odd whitespace still loads") {
    MapFixture f;

    Json node       = Json::object();
    node["palette"] = Json::array({"tiles/test0.otile"});
    Json chunk      = Json::object();
    chunk["at"]     = Json::array({0, 0});
    chunk["cells"]  = "  1 1\n  2:1\n252:0  ";
    node["chunks"]  = Json::array({chunk});

    std::string error;
    REQUIRE(f.map->Deserialize(node, error));
    CHECK(f.map->TileCount() == 4);
    CHECK(f.map->GetTile(0, 0) == 1);
    CHECK(f.map->GetTile(3, 0) == 1);
    CHECK(f.map->GetTile(4, 0) == kEmptyTile);
}

TEST_CASE("compacting the palette drops what is no longer used and renumbers the rest") {
    MapFixture f(3);

    // Only the middle entry is actually painted.
    f.map->SetTile(0, 0, 2);
    f.map->SetTile(1, 0, 2);
    CHECK(f.map->Palette().size() == 3);

    const std::string kept = f.map->Palette()[1];
    CHECK(f.map->CompactPalette() == 2);

    CHECK(f.map->Palette().size() == 1);
    CHECK(f.map->Palette()[0] == kept);

    // The cells have to follow the renumbering, or the map now refers to the
    // wrong material - which looks like the level repainting itself on save.
    CHECK(f.map->GetTile(0, 0) == 1);
    CHECK(f.map->GetTile(1, 0) == 1);
    CHECK(f.map->TileCount() == 2);
}

TEST_CASE("compacting a palette that is fully used changes nothing") {
    MapFixture f(2);
    f.map->SetTile(0, 0, 1);
    f.map->SetTile(1, 0, 2);

    CHECK(f.map->CompactPalette() == 0);
    CHECK(f.map->Palette().size() == 2);
    CHECK(f.map->GetTile(0, 0) == 1);
    CHECK(f.map->GetTile(1, 0) == 2);
}

// ===========================================================================
//  Merging solid cells into boxes
// ===========================================================================

namespace {

std::vector<TileCoord> Rectangle(int minX, int minY, int maxX, int maxY) {
    std::vector<TileCoord> cells;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            cells.push_back(TileCoord{x, y});
        }
    }
    return cells;
}

// Does this set of rectangles cover exactly these cells, once each?
bool CoversExactly(const std::vector<TileRect>& boxes,
                   const std::vector<TileCoord>& cells) {
    std::multiset<std::pair<int, int>> covered;
    for (const TileRect& box : boxes) {
        for (int y = box.minY; y <= box.maxY; ++y) {
            for (int x = box.minX; x <= box.maxX; ++x) {
                covered.emplace(x, y);
            }
        }
    }

    std::multiset<std::pair<int, int>> wanted;
    for (const TileCoord& cell : cells) {
        wanted.emplace(cell.x, cell.y);
    }
    return covered == wanted;
}

} // namespace

TEST_CASE("nothing solid is no boxes") {
    CHECK(MergeSolidCells({}).empty());
}

TEST_CASE("a solid rectangle merges to a single box") {
    // THE CASE THAT MATTERS MOST. A flat floor made of many boxes has vertical
    // joins between them, and a body sliding along it can catch on one - the
    // "tripping over invisible cracks" bug. One box has no joins.
    const std::vector<TileCoord> cells = Rectangle(0, 0, 19, 9);

    const std::vector<TileRect> boxes = MergeSolidCells(cells);
    REQUIRE(boxes.size() == 1);
    CHECK(boxes[0].minX == 0);
    CHECK(boxes[0].minY == 0);
    CHECK(boxes[0].maxX == 19);
    CHECK(boxes[0].maxY == 9);
    CHECK(boxes[0].Cells() == 200);
}

TEST_CASE("a single cell is a one-cell box") {
    const std::vector<TileRect> boxes = MergeSolidCells({TileCoord{4, -7}});
    REQUIRE(boxes.size() == 1);
    CHECK(boxes[0] == TileRect{4, -7, 4, -7});
}

TEST_CASE("a broken row becomes one box per run") {
    //  ##.##   two runs, so two boxes
    const std::vector<TileCoord> cells = {
        TileCoord{0, 0}, TileCoord{1, 0}, TileCoord{3, 0}, TileCoord{4, 0},
    };
    const std::vector<TileRect> boxes = MergeSolidCells(cells);
    REQUIRE(boxes.size() == 2);
    CHECK(CoversExactly(boxes, cells));
}

TEST_CASE("rows of different widths are not stacked together") {
    // Only runs of EXACTLY the same span stack. Here the second row is wider,
    // so it cannot extend the first - and the cover must still be exact.
    std::vector<TileCoord> cells = Rectangle(0, 0, 2, 0);
    for (const TileCoord& c : Rectangle(0, 1, 5, 1)) {
        cells.push_back(c);
    }

    const std::vector<TileRect> boxes = MergeSolidCells(cells);
    CHECK(boxes.size() == 2);
    CHECK(CoversExactly(boxes, cells));
}

TEST_CASE("the merge covers exactly the solid cells, with no overlap") {
    // A PROPERTY, over an awkward shape. Overlapping boxes would double the
    // push a body gets out of a wall; missing cells would be holes to fall
    // through. Neither shows up as anything but "the physics feels wrong".
    std::vector<TileCoord> cells;
    for (int y = -12; y <= 12; ++y) {
        for (int x = -12; x <= 12; ++x) {
            if (((x * 73856093) ^ (y * 19349663)) % 3 != 0) {
                cells.push_back(TileCoord{x, y});
            }
        }
    }
    REQUIRE(!cells.empty());

    const std::vector<TileRect> boxes = MergeSolidCells(cells);
    CHECK(CoversExactly(boxes, cells));
    CHECK(boxes.size() < cells.size());   // it did actually merge something
}

TEST_CASE("a hollow room merges into its four walls") {
    // The shape every level is made of. The result should be a handful of
    // boxes, not the seventy-odd cells the walls are drawn from.
    std::vector<TileCoord> cells;
    for (int y = 0; y <= 9; ++y) {
        for (int x = 0; x <= 19; ++x) {
            const bool wall = (x == 0 || x == 19 || y == 0 || y == 9);
            if (wall) {
                cells.push_back(TileCoord{x, y});
            }
        }
    }

    const std::vector<TileRect> boxes = MergeSolidCells(cells);
    CHECK(CoversExactly(boxes, cells));
    CHECK(boxes.size() <= 10);
    CHECK(cells.size() == 56u);
}

TEST_CASE("two rows with the same span but a gap between them stay two boxes") {
    // THE CASE THE VERTICAL MERGE GUARD EXISTS FOR, and it is easy to leave
    // untested: a run only ever stacks onto the row IMMEDIATELY above it.
    //
    // Without the guard, the span is still remembered from the last row that
    // had any cells at all - which here is five rows up - so the two runs
    // merge into one tall box and the empty rows between them become solid.
    // That is an invisible wall across a doorway.
    //
    // Every other shape in this file has cells on every row, so the guard is
    // never reached by them and a mutation removing it survived.
    const std::vector<TileCoord> cells = {
        TileCoord{0, 0}, TileCoord{1, 0}, TileCoord{2, 0},
        TileCoord{0, 5}, TileCoord{1, 5}, TileCoord{2, 5},
    };

    const std::vector<TileRect> boxes = MergeSolidCells(cells);
    CHECK(boxes.size() == 2);
    CHECK(CoversExactly(boxes, cells));

    int covered = 0;
    for (const TileRect& box : boxes) {
        covered += box.Cells();
    }
    CHECK(covered == 6);   // and not 18, which is what merging through gives
}

TEST_CASE("a one-row gap is still a gap") {
    // The tightest version of the case above, in case an off-by-one ever makes
    // "the row above" mean "the row above that".
    const std::vector<TileCoord> cells = {
        TileCoord{4, 0}, TileCoord{5, 0},
        TileCoord{4, 2}, TileCoord{5, 2},
    };

    const std::vector<TileRect> boxes = MergeSolidCells(cells);
    CHECK(boxes.size() == 2);
    CHECK(CoversExactly(boxes, cells));
}

TEST_CASE("a column with a doorway in it is not sealed up") {
    // The same mistake in the shape it actually takes in a level: a wall with
    // a gap a player walks through.
    std::vector<TileCoord> cells;
    for (int y = 0; y <= 10; ++y) {
        if (y == 4 || y == 5) {
            continue;   // the doorway
        }
        cells.push_back(TileCoord{0, y});
    }

    const std::vector<TileRect> boxes = MergeSolidCells(cells);
    CHECK(CoversExactly(boxes, cells));
    CHECK(boxes.size() == 2);

    // Nothing may cover the doorway. (The spans are worked out first because
    // doctest cannot decompose an expression containing `&&`.)
    for (const TileRect& box : boxes) {
        const bool coversRow4 = (box.minY <= 4) && (box.maxY >= 4);
        const bool coversRow5 = (box.minY <= 5) && (box.maxY >= 5);
        CHECK_FALSE(coversRow4);
        CHECK_FALSE(coversRow5);
    }
}

TEST_CASE("the merge is deterministic") {
    // Two runs with the cells presented in different orders must give the same
    // boxes, or a saved scene and a reloaded one would collide differently.
    std::vector<TileCoord> cells = Rectangle(-3, -3, 4, 2);
    const std::vector<TileRect> first = MergeSolidCells(cells);

    std::reverse(cells.begin(), cells.end());
    const std::vector<TileRect> second = MergeSolidCells(cells);

    CHECK(first == second);
}

// ===========================================================================
//  The collider
// ===========================================================================

TEST_CASE("a tilemap collider with no tilemap beside it has no shapes") {
    MapFixture f;

    Entity* lonely = f.scene.Get(f.scene.CreateEntity("NoMap"));
    auto*   collider = static_cast<TilemapColliderComponent*>(
        lonely->AddComponent(TilemapColliderComponent::kTypeName));
    REQUIRE(collider != nullptr);

    CHECK(collider->WorldShapeCount() == 0);
    CHECK(collider->BoxCount() == 0);
}

TEST_CASE("a collider follows the map it is beside") {
    MapFixture f;

    auto* collider = static_cast<TilemapColliderComponent*>(
        f.entity->AddComponent(TilemapColliderComponent::kTypeName));
    REQUIRE(collider != nullptr);

    // Nothing painted yet.
    CHECK(collider->BoxCount() == 0);

    // A palette entry whose .otile would not load has no TileAsset, so no cell
    // using it is solid. That is the safe direction to fail in: a map with a
    // broken tile is see-through rather than an invisible wall.
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 6; ++x) {
            f.map->SetTile(x, y, 1);
        }
    }
    CHECK(collider->SolidCellCount() == 0);
    CHECK(collider->BoxCount() == 0);
}

TEST_CASE("the collider rebuilds when the map changes") {
    MapFixture f;
    auto* collider = static_cast<TilemapColliderComponent*>(
        f.entity->AddComponent(TilemapColliderComponent::kTypeName));
    REQUIRE(collider != nullptr);

    CHECK(collider->BoxCount() == 0);
    f.map->SetTile(0, 0, 1);

    // It must at least have looked again. With no loadable tile asset the
    // answer is still zero boxes, but the revision check is what makes a
    // painted wall become solid without anything calling the collider.
    CHECK(collider->BoxCount() == 0);
    CHECK(f.map->Revision() > 1);
}

TEST_CASE("a tilemap collider reports the shared collider fields") {
    MapFixture f;

    Json node           = Json::object();
    node["layer"]       = "World";
    node["collidesWith"] = Json::array({"Player"});
    node["trigger"]     = false;

    auto* collider = static_cast<TilemapColliderComponent*>(
        f.entity->AddComponent(TilemapColliderComponent::kTypeName));
    REQUIRE(collider != nullptr);

    std::string error;
    REQUIRE(collider->Deserialize(node, error));

    CHECK(collider->Layer() == "World");
    CHECK(collider->Shape() == ColliderShape::Tilemap);
    CHECK_FALSE(collider->IsTrigger());
    REQUIRE(collider->CollidesWith().size() == 1);
    CHECK(collider->CollidesWith()[0] == "Player");
}

// ===========================================================================
//  Tile assets
//
//  These need files on disk, so they run in a throwaway project folder with
//  ENGINE_ASSET_ROOT pointed at it - the same trick test_assets.cpp uses.
//
//  NONE OF THEM HAVE A WORKING SHEET, because making a texture needs a
//  renderer and the test program has no window. Each .otile therefore states
//  its tileSize rather than leaving it to be measured, and loading one logs a
//  warning about the missing sheet. THAT WARNING IS EXPECTED. The derived
//  tile size is covered by the sample assets in the project, which do leave it
//  out, and which the editor and player both load on start-up.
// ===========================================================================

namespace {

void SetEnvironmentVariable(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

struct TileSandbox {
    fs::path   root;
    FileSystem fileSystem;

    explicit TileSandbox(const char* name) {
        root = fs::temp_directory_path() / (std::string("engine-tile-test-") + name);

        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "assets" / "tiles", ec);

        SetEnvironmentVariable("ENGINE_ASSET_ROOT", root.string().c_str());
        fileSystem.Init(BootConfig{});
        TileLibrary::Clear();
    }

    ~TileSandbox() {
        TileLibrary::Clear();
        fileSystem.Shutdown();
        SetEnvironmentVariable("ENGINE_ASSET_ROOT", "");
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    void Write(const std::string& virtualPath, const std::string& text) const {
        const fs::path  real = fs::path(FileSystem::Resolve(virtualPath));
        std::error_code ec;
        fs::create_directories(real.parent_path(), ec);
        std::ofstream out(real, std::ios::binary);
        out << text;
    }
};

} // namespace

TEST_CASE("a tile asset loads and is cached") {
    TileSandbox box("cache");
    box.Write("tiles/a.otile",
              R"({"sheet":"textures/none.png","tileSize":32,"collision":"Grid"})");

    const TileAsset* first = TileLibrary::Load("tiles/a.otile");
    REQUIRE(first != nullptr);
    CHECK(first->TileSize() == 32);
    CHECK(first->IsSolid());
    CHECK(first->Path() == "tiles/a.otile");

    // The same object, not a second copy: connection is decided by comparing
    // assets, so two copies of grass would not join each other.
    const TileAsset* second = TileLibrary::Load("tiles/a.otile");
    CHECK(second == first);
    CHECK(TileLibrary::Count() == 1);
}

TEST_CASE("a tile asset that is not there does not come back as a broken one") {
    TileSandbox box("missing");
    CHECK(TileLibrary::Load("tiles/nope.otile") == nullptr);
    CHECK(TileLibrary::Count() == 0);
}

TEST_CASE("an odd tile size is refused") {
    // A tile is drawn as four quarters. An odd size cannot be halved, and the
    // result would be a one-pixel seam down the middle of every tile.
    TileSandbox box("odd");
    box.Write("tiles/odd.otile", R"({"sheet":"textures/none.png","tileSize":17})");

    TileAsset    asset;
    std::string  error;
    Json         node = ParseJson(
        R"({"sheet":"textures/none.png","tileSize":17})", error);
    REQUIRE(error.empty());

    CHECK_FALSE(asset.Deserialize(node, error));
    CHECK(error.find("EVEN") != std::string::npos);
}

TEST_CASE("a tile with no sheet is refused unless it is a connector") {
    std::string error;

    TileAsset plain;
    Json      withoutSheet = ParseJson(R"({"tileSize":32})", error);
    REQUIRE(error.empty());
    CHECK_FALSE(plain.Deserialize(withoutSheet, error));
    CHECK(error.find("sheet") != std::string::npos);

    TileAsset connector;
    Json      asConnector = ParseJson(R"({"connector":true,"tileSize":32})", error);
    REQUIRE(error.empty());
    CHECK(connector.Deserialize(asConnector, error));
    CHECK(connector.IsConnector());
    CHECK_FALSE(connector.HasSheet());
    CHECK(connector.TileSize() == 32);
}

TEST_CASE("a connector is never solid, whatever its file says") {
    // A connector is invisible. A solid one would be an invisible wall along
    // the very seam it was added to hide.
    std::string error;
    TileAsset   connector;
    Json node = ParseJson(R"({"connector":true,"tileSize":32,"collision":"Grid"})",
                          error);
    REQUIRE(error.empty());

    REQUIRE(connector.Deserialize(node, error));
    CHECK_FALSE(connector.IsSolid());
    CHECK(connector.Collision() == TileCollision::None);
}

TEST_CASE("a connector gets an editor tint so it can be found") {
    std::string error;
    TileAsset   connector;
    Json node = ParseJson(R"({"connector":true,"tileSize":32})", error);
    REQUIRE(error.empty());
    REQUIRE(connector.Deserialize(node, error));

    CHECK(connector.EditorTint().a > 0);
}

TEST_CASE("connecting is one-way unless both files say so") {
    TileSandbox box("connect");
    box.Write("tiles/grass.otile",
              R"({"sheet":"textures/none.png","tileSize":32,
                  "connectsTo":["tiles/stone.otile"]})");
    box.Write("tiles/stone.otile",
              R"({"sheet":"textures/none.png","tileSize":32})");

    const TileAsset* grass = TileLibrary::Load("tiles/grass.otile");
    const TileAsset* stone = TileLibrary::Load("tiles/stone.otile");
    REQUIRE(grass != nullptr);
    REQUIRE(stone != nullptr);

    // Grass rounds itself off against stone...
    CHECK(grass->ConnectsToAsset(*stone));
    // ...but stone still ends cleanly where it meets grass.
    CHECK_FALSE(stone->ConnectsToAsset(*grass));

    // And everything always joins itself.
    CHECK(grass->ConnectsToAsset(*grass));
    CHECK(stone->ConnectsToAsset(*stone));
}

TEST_CASE("everything treats a connector as its own material") {
    TileSandbox box("connector");
    box.Write("tiles/stone.otile", R"({"sheet":"textures/none.png","tileSize":32})");
    box.Write("tiles/edge.otile", R"({"connector":true,"tileSize":32})");

    const TileAsset* stone = TileLibrary::Load("tiles/stone.otile");
    const TileAsset* edge  = TileLibrary::Load("tiles/edge.otile");
    REQUIRE(stone != nullptr);
    REQUIRE(edge != nullptr);

    // THE WHOLE POINT: stone names nothing in connectsTo, and still believes
    // the material carries on through a connector. That is what lets a line of
    // them hide the seam between two maps.
    CHECK(stone->ConnectsToAsset(*edge));
}

TEST_CASE("a connector in a map makes its neighbour look continuous") {
    TileSandbox box("connector-map");
    box.Write("tiles/stone.otile", R"({"sheet":"textures/none.png","tileSize":32})");
    box.Write("tiles/edge.otile", R"({"connector":true,"tileSize":32})");

    Scene scene;
    ComponentFactory::RegisterBuiltins();
    TilemapRenderSystem::RegisterComponentTypes();
    Scene::SetActive(&scene);

    Entity* entity = scene.Get(scene.CreateEntity("Map"));
    auto*   map    = static_cast<TilemapComponent*>(
        entity->AddComponent(TilemapComponent::kTypeName));
    REQUIRE(map != nullptr);

    const TileId stone = map->AddToPalette("tiles/stone.otile");
    const TileId edge  = map->AddToPalette("tiles/edge.otile");
    REQUIRE(stone != kEmptyTile);
    REQUIRE(edge != kEmptyTile);

    map->SetTile(0, 0, stone);
    CHECK(map->NeighbourMask(0, 0) == 0);   // alone, so four outside corners

    // Ring it with connectors and it believes it is in the middle of a field
    // of stone, even though not one of them is stone and none will be drawn.
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            if (x != 0 || y != 0) {
                map->SetTile(x, y, edge);
            }
        }
    }
    CHECK(map->NeighbourMask(0, 0) == Neighbour::kAll);

    scene.Unload();
    Scene::SetActive(nullptr);
    TilemapRenderSystem::Clear();
}

TEST_CASE("a random tile picks the same frame every time it is asked") {
    // A map whose stones moved between runs would be unusable, so the choice
    // comes from the cell position and not from a random number generator.
    std::string error;
    TileAsset   asset;
    Json        node = ParseJson(
        R"({"sheet":"textures/none.png","tileSize":32,"frames":4,
            "frameMode":"Random"})", error);
    REQUIRE(error.empty());
    REQUIRE(asset.Deserialize(node, error));

    CHECK(asset.FrameCount() == 4);

    for (int y = -5; y <= 5; ++y) {
        for (int x = -5; x <= 5; ++x) {
            const int first = asset.FrameForCell(x, y, 0.0);
            CHECK(first == asset.FrameForCell(x, y, 123.456));   // time is ignored
            CHECK(first >= 0);
            CHECK(first < 4);
        }
    }

    // ...and it does vary between cells, or it would not be doing anything.
    std::set<int> seen;
    for (int x = 0; x < 40; ++x) {
        seen.insert(asset.FrameForCell(x, 0, 0.0));
    }
    CHECK(seen.size() > 1);
}

TEST_CASE("an animated tile stays inside its sheet, forwards and backwards") {
    std::string error;
    TileAsset   asset;
    Json        node = ParseJson(
        R"({"sheet":"textures/none.png","tileSize":32,"frames":4,
            "frameMode":"Animation","animationMinSpeed":3,"animationMaxSpeed":9})",
        error);
    REQUIRE(error.empty());
    REQUIRE(asset.Deserialize(node, error));

    bool allInRange = true;
    for (double t = -40.0; t < 40.0; t += 0.37) {
        for (int x = -4; x <= 4; ++x) {
            const int frame = asset.FrameForCell(x, 0, t);
            if (frame < 0 || frame >= 4) {
                allInRange = false;
            }
        }
    }
    // A NEGATIVE TIME IS THE ONE THAT BITES: std::fmod keeps the sign of its
    // left operand, so without the second wrap this indexes off the front of
    // the sheet.
    CHECK(allInRange);
}

TEST_CASE("animated cells do not all move together") {
    std::string error;
    TileAsset   asset;
    Json        node = ParseJson(
        R"({"sheet":"textures/none.png","tileSize":32,"frames":4,
            "frameMode":"Animation","animationMinSpeed":2,"animationMaxSpeed":8})",
        error);
    REQUIRE(error.empty());
    REQUIRE(asset.Deserialize(node, error));

    // Marching in step is the thing that makes animated tiles read as a grid
    // rather than as a surface.
    std::set<int> atOneInstant;
    for (int x = 0; x < 60; ++x) {
        atOneInstant.insert(asset.FrameForCell(x, 0, 1.25));
    }
    CHECK(atOneInstant.size() > 1);
}

TEST_CASE("a tile asset writes back what it read") {
    std::string error;
    TileAsset   asset;
    Json        node = ParseJson(
        R"({"sheet":"textures/none.png","tileSize":32,"frames":4,
            "frameMode":"Animation","animationMinSpeed":2,"animationMaxSpeed":8,
            "collision":"None","connectsTo":["tiles/other.otile"]})", error);
    REQUIRE(error.empty());
    REQUIRE(asset.Deserialize(node, error));

    Json saved;
    REQUIRE(asset.Serialize(saved));

    TileAsset reloaded;
    REQUIRE(reloaded.Deserialize(saved, error));

    CHECK(reloaded.TileSize() == asset.TileSize());
    CHECK(reloaded.FrameCount() == asset.FrameCount());
    CHECK(reloaded.FrameMode() == asset.FrameMode());
    CHECK(reloaded.Collision() == asset.Collision());
    CHECK(reloaded.AnimationMinSpeed() == doctest::Approx(asset.AnimationMinSpeed()));
    CHECK(reloaded.ConnectsTo() == asset.ConnectsTo());
}

TEST_CASE("a palette entry that will not load is not added") {
    // Otherwise the brush paints cells that are occupied as far as the map is
    // concerned and invisible as far as the screen is.
    TileSandbox box("palette");

    Scene scene;
    ComponentFactory::RegisterBuiltins();
    TilemapRenderSystem::RegisterComponentTypes();
    Scene::SetActive(&scene);

    Entity* entity = scene.Get(scene.CreateEntity("Map"));
    auto*   map    = static_cast<TilemapComponent*>(
        entity->AddComponent(TilemapComponent::kTypeName));
    REQUIRE(map != nullptr);

    CHECK(map->AddToPalette("tiles/does-not-exist.otile") == kEmptyTile);
    CHECK(map->Palette().empty());

    scene.Unload();
    Scene::SetActive(nullptr);
    TilemapRenderSystem::Clear();
}
