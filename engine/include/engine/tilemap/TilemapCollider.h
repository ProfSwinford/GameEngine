#pragma once

// ============================================================================
//  TilemapCollider.h - making the solid tiles of a map solid.
//
//  Put this beside a TilemapComponent and every cell whose tile says
//  "collision": "Grid" becomes something the physics system can push against.
//  Nothing else is needed: it watches the map's revision and rebuilds itself
//  when a tile is painted, so a wall drawn in the editor is a wall.
//
//  ==========================================================================
//  ONE COLLIDER, NOT ONE PER TILE
//
//  The obvious implementation gives every solid cell its own box. A modest
//  level is twenty thousand solid cells, and that would be twenty thousand
//  colliders in the broad phase, twenty thousand entries to sort, and a
//  character walking along a flat floor generating a contact with every cell
//  it passes over.
//
//  It is also WRONG in a way that is hard to see and very easy to feel. Where
//  two boxes meet there is a vertical edge between them, and a box being
//  pushed along the floor can catch on that edge - the famous "my player trips
//  over invisible cracks in a flat floor" bug. A flat floor should be ONE box,
//  because then it has no cracks.
//
//  So the solid cells are MERGED into as few rectangles as will cover them:
//  first into horizontal runs, then runs of identical width stacked into
//  blocks. A twenty-by-ten room of solid floor becomes one box. The editor
//  shows the ratio, and it is typically between ten and a hundred to one.
//
//  ==========================================================================
//  WHAT IT IS NOT
//
//  It is NOT per-tile collision shapes. Fang can trace a collider from a
//  sprite's own pixels; this cannot, and a tile is either solid over its whole
//  cell or not solid at all. For a tile grid that is what almost everybody
//  wants, and it is the shape that merges - an outline traced per tile could
//  not be combined with its neighbour's and would bring back every problem
//  above.
//
//  Sloped tiles would need exactly that, which is the real reason slopes are
//  not here. See the note at the bottom of TileAsset.h.
// ============================================================================

#include <engine/physics/Collider.h>
#include <engine/tilemap/Tilemap.h>

#include <vector>

namespace eng {

class TilemapColliderComponent final : public ColliderComponent {
public:
    static constexpr const char* kTypeName = "TilemapColliderComponent";

    const char*   TypeName() const override { return kTypeName; }
    ColliderShape Shape() const override { return ColliderShape::Tilemap; }

    // Only the shared collider fields - layer, collidesWith, trigger, offset,
    // enabled, friction, bounciness, density. WHICH CELLS ARE SOLID IS NOT ONE
    // OF THEM: that is the tile's business, and saying it twice would let a
    // map and its tiles disagree about what a wall is.
    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;

    int   WorldShapes(ConvexShape* out, int maxOut) const override;
    int   WorldShapeCount() const override;
    float Area() const override;

    // ---- what the merge produced, for the editor and the tests ------------

    // Solid cells before merging, and boxes after. The second divided into the
    // first is how much work the merge saved.
    std::size_t SolidCellCount() const;
    std::size_t BoxCount() const;

    // Forces the next query to rebuild. Normally unnecessary - the map's
    // revision drives it - but the editor calls it after a .otile is edited,
    // because a tile changing from decoration to solid changes which cells are
    // solid without any cell changing.
    void Invalidate();

private:
    // The map this belongs to: the TilemapComponent on the SAME ENTITY.
    //
    // Same entity only, deliberately, and for the same reason
    // ColliderComponent::AttachedRigidbody is: searching up the parent chain
    // would mean a collider's meaning depended on where its entity had been
    // dragged to in the hierarchy.
    const TilemapComponent* Map() const;

    // Rebuilds m_boxes if the map has changed since the last time.
    void EnsureBuilt() const;

    // All mutable, because WorldShapes is const and callers rightly think of
    // asking a collider for its geometry as a read. The cache is an
    // implementation detail of answering that question quickly.
    mutable std::vector<AABB>      m_boxes;          // in the map's own space
    mutable unsigned long long     m_builtRevision = 0;
    mutable Vec2                   m_builtCellSize{0.0f, 0.0f};
    mutable std::size_t            m_solidCells = 0;
    mutable bool                   m_built      = false;

    // So the "there is no tilemap on this entity" complaint is made once and
    // not once per frame.
    mutable bool m_warnedAboutMissingMap = false;
};

// Greedy rectangle cover: the merge described at the top of this file, exposed
// so it can be tested on its own.
//
// `solid` is a list of solid cells in any order; the result is a set of
// rectangles, given as inclusive cell ranges, that covers exactly those cells
// with no overlap. Deterministic: the same input always gives the same output.
struct TileRect {
    int minX = 0;
    int minY = 0;
    int maxX = 0;   // inclusive
    int maxY = 0;   // inclusive

    int Width()  const { return maxX - minX + 1; }
    int Height() const { return maxY - minY + 1; }
    int Cells()  const { return Width() * Height(); }

    friend bool operator==(const TileRect&, const TileRect&) = default;
};

std::vector<TileRect> MergeSolidCells(std::vector<TileCoord> solid);

} // namespace eng
