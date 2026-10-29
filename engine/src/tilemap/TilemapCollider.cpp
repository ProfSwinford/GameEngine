#include <engine/tilemap/TilemapCollider.h>

#include <engine/core/Log.h>

#include <algorithm>
#include <map>
#include <utility>

namespace eng {

// ---------------------------------------------------------------------------
//  The merge
//
//  Two passes, and the second is the one that matters.
//
//  PASS ONE turns each row of solid cells into horizontal RUNS. A row like
//  `..####.##.` becomes two runs. On its own that already collapses a flat
//  floor into one box per row.
//
//  PASS TWO stacks runs. A run on row y whose span is exactly the span of a
//  run on row y-1 extends that one downward instead of starting a new box. A
//  solid twenty-by-ten room is twenty runs of the same span, so it comes out
//  as ONE box.
//
//  "Exactly the span" is doing real work there. Allowing partial overlaps
//  would find a slightly better cover, but finding the BEST cover of an
//  arbitrary shape by rectangles is a genuinely hard problem, and this gets
//  most of the benefit for twenty lines. The shapes it does worst on - long
//  diagonal staircases - are also the ones where a human would not do much
//  better.
// ---------------------------------------------------------------------------

std::vector<TileRect> MergeSolidCells(std::vector<TileCoord> solid) {
    std::vector<TileRect> boxes;
    if (solid.empty()) {
        return boxes;
    }

    // Row-major, so a row's cells end up next to each other and in order. The
    // whole of pass one is then a single walk.
    std::sort(solid.begin(), solid.end(), [](const TileCoord& a, const TileCoord& b) {
        return (a.y != b.y) ? (a.y < b.y) : (a.x < b.x);
    });

    // A run's span (minX, maxX) -> where its box lives in `boxes`. Two of them:
    // what the previous row left open, and what this row is leaving open.
    std::map<std::pair<int, int>, std::size_t> openPrevious;
    std::map<std::pair<int, int>, std::size_t> openCurrent;

    std::size_t index = 0;
    while (index < solid.size()) {
        const int row = solid[index].y;
        openCurrent.clear();

        while (index < solid.size() && solid[index].y == row) {
            const int runStart = solid[index].x;
            int       runEnd   = runStart;

            // Extend while the next cell is the very next column. Duplicates
            // are skipped rather than treated as a break - the same cell
            // listed twice is a caller's mistake, not a hole in the floor.
            while (index + 1 < solid.size() && solid[index + 1].y == row &&
                   (solid[index + 1].x == runEnd + 1 || solid[index + 1].x == runEnd)) {
                ++index;
                runEnd = solid[index].x;
            }
            ++index;

            const std::pair<int, int> span{runStart, runEnd};

            // Does the row above end with a box of exactly this span, finishing
            // on the row immediately above this one?
            if (const auto it = openPrevious.find(span); it != openPrevious.end()) {
                TileRect& box = boxes[it->second];
                if (box.maxY == row - 1) {
                    box.maxY = row;
                    openCurrent.emplace(span, it->second);
                    continue;
                }
            }

            boxes.push_back(TileRect{runStart, row, runEnd, row});
            openCurrent.emplace(span, boxes.size() - 1);
        }

        // Anything in openPrevious that was not extended is simply forgotten,
        // which closes it. Its box is already in `boxes` and already correct.
        openPrevious.swap(openCurrent);
    }

    return boxes;
}

// ---------------------------------------------------------------------------
//  The component
// ---------------------------------------------------------------------------

const TilemapComponent* TilemapColliderComponent::Map() const {
    const Entity* owner = Owner();
    if (owner == nullptr) {
        return nullptr;
    }
    return owner->Find<TilemapComponent>();
}

void TilemapColliderComponent::Invalidate() {
    m_built = false;
}

void TilemapColliderComponent::EnsureBuilt() const {
    const TilemapComponent* map = Map();
    if (map == nullptr) {
        if (!m_warnedAboutMissingMap) {
            m_warnedAboutMissingMap = true;
            ENGINE_LOG_ERROR(Channels::kTilemap,
                             "a TilemapColliderComponent has no TilemapComponent on "
                             "the same entity, so it has nothing to be solid. Add "
                             "one, or remove the collider.");
        }
        m_boxes.clear();
        m_solidCells = 0;
        m_built      = true;
        return;
    }

    // Rebuilt when a cell changes, and also when the cell SIZE changes - the
    // boxes are in world units, so resizing the grid moves every one of them
    // even though no cell was touched.
    const Vec2 cellSize = map->CellSize();
    if (m_built && m_builtRevision == map->Revision() &&
        m_builtCellSize.x == cellSize.x && m_builtCellSize.y == cellSize.y) {
        return;
    }

    m_builtRevision = map->Revision();
    m_builtCellSize = cellSize;
    m_built         = true;
    m_boxes.clear();

    TileCoord lo{};
    TileCoord hi{};
    if (!map->TileBounds(lo, hi)) {
        m_solidCells = 0;
        return;
    }

    // Gathered by walking the painted area. A tile whose asset says "None" is
    // decoration and is left out here, which is what makes a background layer
    // of grass cost nothing at all.
    std::vector<TileCoord> solid;
    solid.reserve(map->TileCount());
    for (int y = lo.y; y <= hi.y; ++y) {
        for (int x = lo.x; x <= hi.x; ++x) {
            const TileId tile = map->GetTile(x, y);
            if (tile == kEmptyTile) {
                continue;
            }
            const TileAsset* asset = map->AssetFor(tile);
            if (asset == nullptr || !asset->IsSolid()) {
                continue;
            }
            solid.push_back(TileCoord{x, y});
        }
    }

    m_solidCells = solid.size();

    const std::vector<TileRect> merged = MergeSolidCells(std::move(solid));
    m_boxes.reserve(merged.size());
    for (const TileRect& rect : merged) {
        // Inclusive cell ranges, so the far edge is the FAR side of cell maxX,
        // which is (maxX + 1) cell widths from the origin. Using maxX here
        // would make every box one cell short in each direction - a gap you
        // could walk through at the right-hand end of every wall.
        m_boxes.push_back(AABB{
            Vec2{static_cast<float>(rect.minX) * cellSize.x,
                 static_cast<float>(rect.minY) * cellSize.y},
            Vec2{static_cast<float>(rect.maxX + 1) * cellSize.x,
                 static_cast<float>(rect.maxY + 1) * cellSize.y},
        });
    }
}

int TilemapColliderComponent::WorldShapeCount() const {
    EnsureBuilt();
    return static_cast<int>(m_boxes.size());
}

int TilemapColliderComponent::WorldShapes(ConvexShape* out, int maxOut) const {
    EnsureBuilt();

    const Mat3  world    = OwnerWorldMatrix();
    const Vec2  scale    = world.GetScale();
    const float rotation = world.GetRotation();

    int written = 0;
    for (const AABB& local : m_boxes) {
        if (written >= maxOut) {
            break;
        }

        // An OBB rather than an AABB, so a rotated tilemap has rotated solid
        // boxes rather than the larger upright boxes around them. Overlap.h
        // gained real oriented-box support with the physics rewrite, so this
        // costs nothing extra.
        const Vec2 centre = world.TransformPoint(m_offset + local.Center());
        const Vec2 half{local.Extents().x * scale.x, local.Extents().y * scale.y};

        out[written++] = ToConvex(OBB{centre, half, rotation});
    }
    return written;
}

float TilemapColliderComponent::Area() const {
    EnsureBuilt();

    float total = 0.0f;
    for (const AABB& box : m_boxes) {
        const Vec2 size = box.Size();
        total += size.x * size.y;
    }
    return total;
}

std::size_t TilemapColliderComponent::SolidCellCount() const {
    EnsureBuilt();
    return m_solidCells;
}

std::size_t TilemapColliderComponent::BoxCount() const {
    EnsureBuilt();
    return m_boxes.size();
}

bool TilemapColliderComponent::Deserialize(const Json& node, std::string& outError) {
    // The shared fields and nothing else. A tilemap collider has no shape of
    // its own to read - its shape is the map beside it.
    if (!ColliderComponent::Deserialize(node, outError)) {
        return false;
    }
    Invalidate();
    return true;
}

bool TilemapColliderComponent::Serialize(Json& out) const {
    return ColliderComponent::Serialize(out);
}

} // namespace eng
