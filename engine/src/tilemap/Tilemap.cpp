#include <engine/tilemap/Tilemap.h>

#include <engine/core/Log.h>
#include <engine/core/Time.h>
#include <engine/render/Camera.h>
#include <engine/tilemap/TilemapCollider.h>

#include <algorithm>
#include <charconv>
#include <cmath>

namespace eng {

namespace {

// ---------------------------------------------------------------------------
//  Chunk arithmetic
// ---------------------------------------------------------------------------

static_assert((kTilemapChunkSize & (kTilemapChunkSize - 1)) == 0,
              "kTilemapChunkSize must be a power of two: the cell-to-chunk "
              "conversion below is a shift and a mask, which is what makes "
              "negative coordinates work without a branch");

constexpr int ChunkShift() {
    int shift = 0;
    for (int size = kTilemapChunkSize; size > 1; size >>= 1) {
        ++shift;
    }
    return shift;
}

constexpr int kChunkShift = ChunkShift();
constexpr int kChunkMask  = kTilemapChunkSize - 1;

// ---------------------------------------------------------------------------
//  Counters, read back through TilemapRenderSystem
// ---------------------------------------------------------------------------
std::size_t g_cellsDrawn  = 0;
std::size_t g_cellsSkipped = 0;

// Every registered tilemap. Borrowed pointers, as in SpriteRenderSystem: the
// component owns itself and takes its entry out in OnDetach.
std::vector<TilemapComponent*>& Registry() {
    // In a function rather than at file scope: the engine DLL exports functions
    // and NOT data. See engine/CMakeLists.txt.
    static std::vector<TilemapComponent*> tilemaps;
    return tilemaps;
}

Color ReadTint(const Json& node, std::string_view key, Color fallback) {
    if (!HasKey(node, key)) {
        return fallback;
    }
    const Json& value = node[std::string(key)];
    if (!value.is_array() || value.size() != 4) {
        ENGINE_LOG_WARN(Channels::kTilemap,
                        "TilemapComponent.{} should be four numbers like "
                        "[255, 255, 255, 255]; leaving it alone", key);
        return fallback;
    }
    auto channel = [](const Json& entry) {
        return static_cast<unsigned char>(std::clamp(
            entry.is_number() ? entry.get<float>() : 255.0f, 0.0f, 255.0f));
    };
    return Color{channel(value[0]), channel(value[1]), channel(value[2]),
                 channel(value[3])};
}

// Two tints, one on top of the other. Multiplying is what a tint MEANS - white
// leaves the picture alone - so combining two of them is multiplying them.
Color MultiplyTint(Color a, Color b) {
    auto mul = [](unsigned char lhs, unsigned char rhs) {
        return static_cast<unsigned char>((static_cast<int>(lhs) * rhs) / 255);
    };
    return Color{mul(a.r, b.r), mul(a.g, b.g), mul(a.b, b.b), mul(a.a, b.a)};
}

// ---------------------------------------------------------------------------
//  Run-length coding for a chunk's cells
//
//  A chunk is 256 cells and nearly all of them repeat, so writing them out one
//  per line would be 256 lines of mostly the same number. The encoding is
//  whitespace-separated runs, each either a bare value or `count:value`:
//
//      "16:0 3:1 2 48:0 ..."
//
//  CHOSEN TO BE READABLE, not to be small. A scene file is something people
//  open, diff and occasionally fix by hand, and a base64 blob is none of those
//  things. An empty chunk is the four characters "256:0".
// ---------------------------------------------------------------------------

std::string EncodeCells(const std::array<TileId, kTilemapChunkCells>& cells) {
    std::string out;
    std::size_t index = 0;
    while (index < cells.size()) {
        const TileId value = cells[index];

        std::size_t run = 1;
        while (index + run < cells.size() && cells[index + run] == value) {
            ++run;
        }

        if (!out.empty()) {
            out.push_back(' ');
        }
        if (run > 1) {
            out += std::to_string(run);
            out.push_back(':');
        }
        out += std::to_string(value);

        index += run;
    }
    return out;
}

bool ParseUnsigned(std::string_view text, unsigned long& outValue) {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end   = text.data() + text.size();
    const auto  result = std::from_chars(begin, end, outValue);
    return result.ec == std::errc{} && result.ptr == end;
}

bool DecodeCells(std::string_view text, std::array<TileId, kTilemapChunkCells>& out,
                 std::string& outError) {
    out.fill(kEmptyTile);

    std::size_t written = 0;
    std::size_t cursor  = 0;
    while (cursor < text.size()) {
        // Skip any run of whitespace, so hand-edited files with newlines or
        // double spaces in them still load.
        while (cursor < text.size() &&
               (text[cursor] == ' ' || text[cursor] == '\t' ||
                text[cursor] == '\n' || text[cursor] == '\r')) {
            ++cursor;
        }
        if (cursor >= text.size()) {
            break;
        }

        std::size_t tokenEnd = cursor;
        while (tokenEnd < text.size() && text[tokenEnd] != ' ' &&
               text[tokenEnd] != '\t' && text[tokenEnd] != '\n' &&
               text[tokenEnd] != '\r') {
            ++tokenEnd;
        }

        const std::string_view token = text.substr(cursor, tokenEnd - cursor);
        cursor                       = tokenEnd;

        unsigned long count = 1;
        unsigned long value = 0;

        const std::size_t colon = token.find(':');
        if (colon == std::string_view::npos) {
            if (!ParseUnsigned(token, value)) {
                outError = "'" + std::string(token) +
                           "' is not a cell value; a run is either a number or "
                           "count:number";
                return false;
            }
        } else {
            if (!ParseUnsigned(token.substr(0, colon), count) ||
                !ParseUnsigned(token.substr(colon + 1), value)) {
                outError = "'" + std::string(token) +
                           "' is not a valid count:number run";
                return false;
            }
        }

        if (count == 0) {
            outError = "'" + std::string(token) + "' has a run length of zero";
            return false;
        }
        if (value > 0xFFFFu) {
            outError = "cell value " + std::to_string(value) +
                       " is larger than a tile id can hold";
            return false;
        }
        if (written + count > out.size()) {
            outError = "too many cells: a chunk holds exactly " +
                       std::to_string(out.size()) + ", and this run would make " +
                       std::to_string(written + count);
            return false;
        }

        for (unsigned long i = 0; i < count; ++i) {
            out[written++] = static_cast<TileId>(value);
        }
    }

    if (written != out.size()) {
        outError = "a chunk holds exactly " + std::to_string(out.size()) +
                   " cells, but this one decoded to " + std::to_string(written);
        return false;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
//  Chunk lookup
// ---------------------------------------------------------------------------

TileCoord TilemapComponent::ChunkOf(int x, int y) {
    // An arithmetic right shift IS floor division for a negative number, which
    // is exactly what a grid with no origin needs: cell -1 belongs to chunk -1,
    // not to chunk 0. Plain `/` rounds toward zero and would put cells -15..15
    // all in chunk 0, folding the map over on itself at the axis.
    return TileCoord{x >> kChunkShift, y >> kChunkShift};
}

int TilemapComponent::IndexInChunk(int x, int y) {
    // ...and `& mask` is the matching always-positive remainder.
    return ((y & kChunkMask) << kChunkShift) | (x & kChunkMask);
}

const TilemapComponent::Chunk* TilemapComponent::FindChunk(TileCoord chunk) const {
    if (m_cachedChunk != nullptr && m_cachedChunkCoord == chunk) {
        return m_cachedChunk;
    }

    const auto it = m_chunks.find(chunk);
    if (it == m_chunks.end()) {
        return nullptr;
    }

    m_cachedChunkCoord = chunk;
    m_cachedChunk      = &it->second;
    return m_cachedChunk;
}

// ---------------------------------------------------------------------------
//  Reading and writing cells
// ---------------------------------------------------------------------------

TileId TilemapComponent::GetTile(int x, int y) const {
    const Chunk* chunk = FindChunk(ChunkOf(x, y));
    if (chunk == nullptr) {
        return kEmptyTile;
    }
    return chunk->cells[static_cast<std::size_t>(IndexInChunk(x, y))];
}

void TilemapComponent::SetTile(int x, int y, TileId tile) {
    const TileCoord chunkCoord = ChunkOf(x, y);
    const auto      index      = static_cast<std::size_t>(IndexInChunk(x, y));

    auto it = m_chunks.find(chunkCoord);

    if (it == m_chunks.end()) {
        // Nothing here yet. Erasing an already-empty cell must NOT create a
        // chunk full of zeroes - that is how a map ends up with a thousand
        // chunks holding nothing after a few passes with the eraser.
        if (tile == kEmptyTile) {
            return;
        }
        it = m_chunks.emplace(chunkCoord, Chunk{}).first;
        m_cachedChunk = nullptr;   // the map may have rehashed its nodes
    }

    Chunk&       chunk    = it->second;
    const TileId previous = chunk.cells[index];
    if (previous == tile) {
        // Painting a cell with what it already holds is not a change. Saying so
        // matters: the revision is what the collider rebuilds on, and dragging
        // a brush across one cell would otherwise rebuild it every frame.
        return;
    }

    chunk.cells[index] = tile;

    if (previous == kEmptyTile) {
        ++chunk.used;
        ++m_tileCount;
    } else if (tile == kEmptyTile) {
        --chunk.used;
        --m_tileCount;
    }

    if (chunk.used == 0) {
        m_chunks.erase(it);
        m_cachedChunk = nullptr;
    }

    ++m_revision;
}

void TilemapComponent::Clear() {
    if (m_chunks.empty()) {
        return;
    }
    m_chunks.clear();
    m_tileCount   = 0;
    m_cachedChunk = nullptr;
    ++m_revision;
}

bool TilemapComponent::TileBounds(TileCoord& outMin, TileCoord& outMax) const {
    if (m_tileCount == 0) {
        return false;
    }

    bool      found = false;
    TileCoord lo{};
    TileCoord hi{};

    // Walked cell by cell rather than chunk by chunk, because a chunk's extent
    // is not the extent of what has been painted in it - one tile in the
    // corner of a chunk would report a box sixteen cells across.
    for (const auto& [chunkCoord, chunk] : m_chunks) {
        for (int local = 0; local < kTilemapChunkCells; ++local) {
            if (chunk.cells[static_cast<std::size_t>(local)] == kEmptyTile) {
                continue;
            }
            const int x = (chunkCoord.x << kChunkShift) + (local & kChunkMask);
            const int y = (chunkCoord.y << kChunkShift) + (local >> kChunkShift);

            if (!found) {
                lo    = TileCoord{x, y};
                hi    = TileCoord{x, y};
                found = true;
            } else {
                lo.x = std::min(lo.x, x);
                lo.y = std::min(lo.y, y);
                hi.x = std::max(hi.x, x);
                hi.y = std::max(hi.y, y);
            }
        }
    }

    if (!found) {
        return false;
    }
    outMin = lo;
    outMax = hi;
    return true;
}

// ---------------------------------------------------------------------------
//  The palette
// ---------------------------------------------------------------------------

const TileAsset* TilemapComponent::AssetFor(TileId tile) const {
    if (tile == kEmptyTile || tile > m_assets.size()) {
        return nullptr;
    }
    return m_assets[static_cast<std::size_t>(tile) - 1];
}

TileId TilemapComponent::FindInPalette(std::string_view otilePath) const {
    for (std::size_t i = 0; i < m_palette.size(); ++i) {
        if (m_palette[i] == otilePath) {
            return static_cast<TileId>(i + 1);
        }
    }
    return kEmptyTile;
}

TileId TilemapComponent::AddToPalette(std::string_view otilePath) {
    if (otilePath.empty()) {
        return kEmptyTile;
    }

    if (const TileId existing = FindInPalette(otilePath); existing != kEmptyTile) {
        return existing;
    }

    // Loaded BEFORE the palette grows, so a path that will not load does not
    // leave an entry behind. A palette slot pointing at nothing would paint
    // cells that are occupied as far as the map is concerned and invisible as
    // far as the screen is concerned - the worst of both.
    if (TileLibrary::Load(otilePath) == nullptr) {
        return kEmptyTile;
    }

    // Cell ids are 16-bit and 0 means empty, so 65535 materials fit in one map.
    if (m_palette.size() >= 0xFFFEu) {
        ENGINE_LOG_ERROR(Channels::kTilemap,
                         "tilemap palette is full ({} entries); '{}' was not "
                         "added. Use a second TilemapComponent.",
                         m_palette.size(), otilePath);
        return kEmptyTile;
    }

    m_palette.emplace_back(otilePath);
    RefreshPalette();
    return static_cast<TileId>(m_palette.size());
}

std::size_t TilemapComponent::CompactPalette() {
    if (m_palette.empty()) {
        return 0;
    }

    std::vector<bool> used(m_palette.size() + 1, false);
    for (const auto& [chunkCoord, chunk] : m_chunks) {
        for (const TileId cell : chunk.cells) {
            if (cell != kEmptyTile && cell < used.size()) {
                used[cell] = true;
            }
        }
    }

    // old id -> new id, with 0 staying 0.
    std::vector<TileId>      remap(m_palette.size() + 1, kEmptyTile);
    std::vector<std::string> kept;
    kept.reserve(m_palette.size());

    for (std::size_t old = 1; old < used.size(); ++old) {
        if (!used[old]) {
            continue;
        }
        kept.push_back(m_palette[old - 1]);
        remap[old] = static_cast<TileId>(kept.size());
    }

    const std::size_t removed = m_palette.size() - kept.size();
    if (removed == 0) {
        return 0;
    }

    for (auto& [chunkCoord, chunk] : m_chunks) {
        for (TileId& cell : chunk.cells) {
            cell = (cell < remap.size()) ? remap[cell] : kEmptyTile;
        }
    }

    m_palette = std::move(kept);
    RefreshPalette();
    return removed;
}

void TilemapComponent::RefreshPalette() {
    m_assets.clear();
    m_assets.reserve(m_palette.size());
    for (const std::string& path : m_palette) {
        m_assets.push_back(TileLibrary::Load(path));
    }

    // ONE ROW AND COLUMN FOR kEmptyTile, so a cell value indexes the table
    // directly with no subtracting. Row 0 and column 0 stay all-zero, which is
    // the right answer: nothing connects to nothing.
    const std::size_t size = m_palette.size() + 1;
    m_connects.assign(size * size, 0);

    for (std::size_t self = 1; self < size; ++self) {
        const TileAsset* assetSelf = m_assets[self - 1];
        for (std::size_t other = 1; other < size; ++other) {
            const TileAsset* assetOther = m_assets[other - 1];

            bool connected = false;
            if (assetSelf != nullptr && assetOther != nullptr) {
                connected = assetSelf->ConnectsToAsset(*assetOther);
            } else {
                // One of them would not load. A tile at least joins itself, so
                // a map with a broken palette entry still draws as a shape
                // rather than as scattered single blocks.
                connected = (self == other);
            }

            m_connects[self * size + other] = connected ? 1 : 0;
        }
    }
}

bool TilemapComponent::Connects(TileId self, TileId other) const {
    const std::size_t size = m_palette.size() + 1;
    if (self == kEmptyTile || other == kEmptyTile) {
        return false;
    }
    if (static_cast<std::size_t>(self) >= size ||
        static_cast<std::size_t>(other) >= size) {
        return false;
    }
    return m_connects[static_cast<std::size_t>(self) * size +
                      static_cast<std::size_t>(other)] != 0;
}

unsigned char TilemapComponent::NeighbourMask(int x, int y) const {
    const TileId self = GetTile(x, y);
    if (self == kEmptyTile) {
        return 0;
    }

    unsigned char mask = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const unsigned char bit = Neighbour::Bit(dx, dy);
            if (bit == 0) {
                continue;   // the centre: a tile is not its own neighbour
            }
            if (Connects(self, GetTile(x + dx, y + dy))) {
                mask = static_cast<unsigned char>(mask | bit);
            }
        }
    }
    return mask;
}

// ---------------------------------------------------------------------------
//  Geometry
// ---------------------------------------------------------------------------

Vec2 TilemapComponent::CellSize() const {
    if (m_cellSizeExplicit && m_cellSize.x > 0.0f && m_cellSize.y > 0.0f) {
        return m_cellSize;
    }

    // Derived from the first palette entry that loaded, so a map set up with
    // nothing but a brush draws at the size the art was drawn at.
    for (const TileAsset* asset : m_assets) {
        if (asset != nullptr && asset->TileSize() > 0) {
            const auto size = static_cast<float>(asset->TileSize());
            return Vec2{size, size};
        }
    }
    return Vec2{16.0f, 16.0f};
}

void TilemapComponent::SetCellSize(Vec2 size) {
    m_cellSize         = size;
    m_cellSizeExplicit = (size.x > 0.0f && size.y > 0.0f);
}

AABB TilemapComponent::CellBoundsLocal(int x, int y) const {
    // Cell (0, 0) has its BOTTOM-LEFT corner on the entity's origin, which is
    // the convention Unity's tilemap uses. Centring it on the origin instead
    // would put half of cell (0,0) in negative space and make every bit of
    // arithmetic in the editor's brush carry a half-cell term.
    const Vec2 size = CellSize();
    const Vec2 min{static_cast<float>(x) * size.x, static_cast<float>(y) * size.y};
    return AABB{min, Vec2{min.x + size.x, min.y + size.y}};
}

Vec2 TilemapComponent::CellToWorld(int x, int y) const {
    const Vec2 centre = CellBoundsLocal(x, y).Center();
    const Transform2D* transform = OwnerTransform();
    if (transform == nullptr) {
        return centre;
    }
    return transform->WorldMatrix().TransformPoint(centre);
}

TileCoord TilemapComponent::WorldToCell(Vec2 world) const {
    Vec2 local = world;
    if (const Transform2D* transform = OwnerTransform(); transform != nullptr) {
        local = transform->WorldMatrix().Inverse().TransformPoint(world);
    }

    const Vec2 size = CellSize();
    if (size.x <= 0.0f || size.y <= 0.0f) {
        return TileCoord{0, 0};
    }

    // std::floor and NOT a cast to int. A cast rounds toward zero, so a point
    // at local x = -0.5 would land in cell 0 along with everything from 0 to
    // 1 - a double-width cell straddling the axis, which shows up as a brush
    // that paints the wrong cell on one side of the origin only.
    return TileCoord{
        static_cast<int>(std::floor(local.x / size.x)),
        static_cast<int>(std::floor(local.y / size.y)),
    };
}

// ---------------------------------------------------------------------------
//  Serialization
// ---------------------------------------------------------------------------

bool TilemapComponent::Deserialize(const Json& node, std::string& outError) {
    m_palette.clear();
    if (HasKey(node, "palette")) {
        const Json& list = node.at("palette");
        if (!list.is_array()) {
            outError = "TilemapComponent.palette must be a list of .otile paths";
            return false;
        }
        for (const Json& entry : list) {
            if (!entry.is_string()) {
                outError = "TilemapComponent.palette entries must each be text, "
                           "e.g. \"tiles/grass.otile\"";
                return false;
            }
            m_palette.push_back(entry.get<std::string>());
        }
    }
    RefreshPalette();

    m_cellSizeExplicit = HasKey(node, "cellSize");
    if (m_cellSizeExplicit) {
        m_cellSize = ReadVec2(node, "cellSize", Vec2{0.0f, 0.0f}, kTypeName);
        if (m_cellSize.x <= 0.0f || m_cellSize.y <= 0.0f) {
            outError = "TilemapComponent.cellSize must be two positive numbers; "
                       "omit it entirely to use the tile's own size";
            return false;
        }
    }

    m_layer = ReadInt(node, "layer", 0, kTypeName);
    m_tint  = ReadTint(node, "tint", Color::White());

    if (!DeserializeChunks(node, outError)) {
        return false;
    }

    outError.clear();
    return true;
}

bool TilemapComponent::DeserializeChunks(const Json& node, std::string& outError) {
    m_chunks.clear();
    m_tileCount   = 0;
    m_cachedChunk = nullptr;
    ++m_revision;

    if (!HasKey(node, "chunks")) {
        return true;   // an empty map is perfectly valid
    }

    const Json& list = node.at("chunks");
    if (!list.is_array()) {
        outError = "TilemapComponent.chunks must be a list";
        return false;
    }

    for (const Json& entry : list) {
        if (!entry.is_object()) {
            outError = "each TilemapComponent.chunks entry must be an object "
                       "with \"at\" and \"cells\"";
            return false;
        }

        const Vec2 at = ReadVec2(entry, "at", Vec2{0.0f, 0.0f}, "TilemapComponent.chunks");
        const TileCoord chunkCoord{static_cast<int>(at.x), static_cast<int>(at.y)};

        const std::string cells = ReadString(entry, "cells", "", "TilemapComponent.chunks");
        if (cells.empty()) {
            outError = "the chunk at (" + std::to_string(chunkCoord.x) + ", " +
                       std::to_string(chunkCoord.y) + ") has no \"cells\"";
            return false;
        }

        Chunk       chunk;
        std::string why;
        if (!DecodeCells(cells, chunk.cells, why)) {
            outError = "the chunk at (" + std::to_string(chunkCoord.x) + ", " +
                       std::to_string(chunkCoord.y) + "): " + why;
            return false;
        }

        chunk.used = 0;
        for (const TileId cell : chunk.cells) {
            if (cell == kEmptyTile) {
                continue;
            }
            ++chunk.used;

            // A cell naming a palette entry that does not exist is a broken
            // file, not something to paper over: the cell is occupied as far
            // as the map is concerned, and invisible as far as the screen is.
            if (static_cast<std::size_t>(cell) > m_palette.size()) {
                outError = "the chunk at (" + std::to_string(chunkCoord.x) + ", " +
                           std::to_string(chunkCoord.y) + ") uses tile id " +
                           std::to_string(cell) + ", but the palette has only " +
                           std::to_string(m_palette.size()) + " entr" +
                           (m_palette.size() == 1 ? "y" : "ies");
                return false;
            }
        }

        if (chunk.used == 0) {
            continue;   // an all-empty chunk is not worth keeping
        }

        m_tileCount += chunk.used;
        m_chunks.emplace(chunkCoord, std::move(chunk));
    }

    return true;
}

bool TilemapComponent::Serialize(Json& out) const {
    out = Json::object();

    out["palette"] = m_palette;

    if (m_cellSizeExplicit) {
        WriteVec2(out, "cellSize", m_cellSize);
    }
    if (m_layer != 0) {
        out["layer"] = m_layer;
    }
    if (m_tint != Color::White()) {
        out["tint"] = Json::array({m_tint.r, m_tint.g, m_tint.b, m_tint.a});
    }

    Json chunks = Json::array();
    for (const auto& [chunkCoord, chunk] : m_chunks) {
        Json entry       = Json::object();
        entry["at"]      = Json::array({chunkCoord.x, chunkCoord.y});
        entry["cells"]   = EncodeCells(chunk.cells);
        chunks.push_back(std::move(entry));
    }
    out["chunks"] = std::move(chunks);

    return true;
}

// ---------------------------------------------------------------------------
//  Drawing
//
//  WHY THE SCREEN RECTANGLE IS BUILT FROM ROUNDED SHARED EDGES
//
//  Each cell is drawn as four quarters, so a 30x20 map is 2400 little
//  rectangles side by side. Position each one independently and the edges land
//  on fractional pixels; the renderer rounds each one on its own, and wherever
//  two neighbours round in opposite directions a one-pixel line of background
//  shows between them. On a wall of solid stone that line is extremely
//  visible, and it MOVES as the camera scrolls, which is worse.
//
//  Fang solves it when baking by extruding the outermost pixel of every tile
//  outward - its "Enable Padding" setting. Nothing is baked here, so instead
//  the boundaries themselves are rounded: a cell's right edge and its
//  neighbour's left edge are the SAME local coordinate, so they round to the
//  same pixel and meet exactly. No gap can appear, because there is no second
//  number to disagree with.
//
//  THAT ONLY WORKS WHILE THE GRID IS AXIS-ALIGNED. A rotated map has no pixel
//  column to snap to, so it is drawn from centres and sizes and may show the
//  seam. Unrotated is the case that has to be perfect.
// ---------------------------------------------------------------------------

void TilemapComponent::Render(Camera& camera, double seconds, bool editorTints) const {
    if (m_chunks.empty()) {
        return;
    }

    const Transform2D* transform = OwnerTransform();
    if (transform == nullptr) {
        return;
    }

    const Vec2 cellSize = CellSize();
    if (cellSize.x <= 0.0f || cellSize.y <= 0.0f) {
        return;
    }

    const Mat3 world   = transform->WorldMatrix();
    const Mat3 toLocal = world.Inverse();

    // ---- which cells can be seen -------------------------------------------
    const AABB visible = camera.VisibleBounds();

    // All FOUR corners, not two. Under rotation the visible region is a tilted
    // rectangle in the map's own space, and two opposite corners of a tilted
    // rectangle do not bound it - using just those clips the corners off the
    // drawn area, which looks like tiles popping in and out at the edges.
    const Vec2 screenCorners[4] = {
        visible.min,
        Vec2{visible.max.x, visible.min.y},
        visible.max,
        Vec2{visible.min.x, visible.max.y},
    };

    Vec2 lo = toLocal.TransformPoint(screenCorners[0]);
    Vec2 hi = lo;
    for (int i = 1; i < 4; ++i) {
        const Vec2 corner = toLocal.TransformPoint(screenCorners[i]);
        lo.x = std::min(lo.x, corner.x);
        lo.y = std::min(lo.y, corner.y);
        hi.x = std::max(hi.x, corner.x);
        hi.y = std::max(hi.y, corner.y);
    }

    // One cell of margin on every side, so a tile only half on screen is still
    // drawn - and so the cells just off screen exist to be read as neighbours.
    const int minCellX = static_cast<int>(std::floor(lo.x / cellSize.x)) - 1;
    const int minCellY = static_cast<int>(std::floor(lo.y / cellSize.y)) - 1;
    const int maxCellX = static_cast<int>(std::floor(hi.x / cellSize.x)) + 1;
    const int maxCellY = static_cast<int>(std::floor(hi.y / cellSize.y)) + 1;

    const float rotation = world.GetRotation();
    const Vec2  scale    = world.GetScale();
    const bool  snapped  = std::abs(rotation) < 1.0e-4f;

    for (const auto& [chunkCoord, chunk] : m_chunks) {
        const int chunkMinX = chunkCoord.x << kChunkShift;
        const int chunkMinY = chunkCoord.y << kChunkShift;
        const int chunkMaxX = chunkMinX + kTilemapChunkSize - 1;
        const int chunkMaxY = chunkMinY + kTilemapChunkSize - 1;

        // Rejecting a whole chunk at once is what makes a large map affordable:
        // a 500x500 map is about a thousand chunks, and all but a handful are
        // dismissed by these four comparisons.
        if (chunkMaxX < minCellX || chunkMinX > maxCellX ||
            chunkMaxY < minCellY || chunkMinY > maxCellY) {
            g_cellsSkipped += chunk.used;
            continue;
        }

        // Counted per chunk so that drawn + skipped adds up to every non-empty
        // cell. A chunk that is only PARTLY on screen still has cells outside
        // the range below, and not counting those made the two numbers
        // disagree with the map's own tile count - which is exactly the kind
        // of statistic that is worse than none at all.
        const std::size_t drawnBefore = g_cellsDrawn;

        const int fromX = std::max(chunkMinX, minCellX);
        const int toX   = std::min(chunkMaxX, maxCellX);
        const int fromY = std::max(chunkMinY, minCellY);
        const int toY   = std::min(chunkMaxY, maxCellY);

        for (int y = fromY; y <= toY; ++y) {
            for (int x = fromX; x <= toX; ++x) {
                const TileId tile =
                    chunk.cells[static_cast<std::size_t>(IndexInChunk(x, y))];
                if (tile == kEmptyTile) {
                    continue;
                }

                const TileAsset* asset = AssetFor(tile);
                if (asset == nullptr) {
                    continue;
                }

                // The four screen boundaries of this cell: left, middle, right
                // and top, middle, bottom. Computed from the cell's own corners
                // in LOCAL space, so the value a cell uses for its right edge
                // is the same value its neighbour uses for its left edge.
                const AABB bounds = CellBoundsLocal(x, y);
                const Vec2 centre = bounds.Center();

                float xs[3];
                float ys[3];
                if (snapped) {
                    const Vec2 screenMin = camera.WorldToScreen(
                        world.TransformPoint(bounds.min));
                    const Vec2 screenMid = camera.WorldToScreen(
                        world.TransformPoint(centre));
                    const Vec2 screenMax = camera.WorldToScreen(
                        world.TransformPoint(bounds.max));

                    xs[0] = std::round(screenMin.x);
                    xs[1] = std::round(screenMid.x);
                    xs[2] = std::round(screenMax.x);

                    // THE Y FLIP: local max.y is the TOP of the cell, which is
                    // the SMALLER screen y. ys[] runs top to bottom.
                    ys[0] = std::round(screenMax.y);
                    ys[1] = std::round(screenMid.y);
                    ys[2] = std::round(screenMin.y);
                }

                const unsigned char  mask  = NeighbourMask(x, y);
                const QuadrantSlots  slots = ResolveQuadrants(mask);
                const int            frame = asset->FrameForCell(x, y, seconds);

                Color tint = m_tint;
                if (editorTints && asset->EditorTint().a != 0) {
                    tint = MultiplyTint(tint, asset->EditorTint());
                }

                // ---- a connector draws nothing, or a marker in the editor ---
                if (!asset->HasSheet()) {
                    if (editorTints && asset->EditorTint().a != 0) {
                        const Vec2 cornerA =
                            camera.WorldToScreen(world.TransformPoint(bounds.min));
                        const Vec2 cornerB =
                            camera.WorldToScreen(world.TransformPoint(bounds.max));
                        Renderer::DrawFilledRect(
                            Vec2{std::min(cornerA.x, cornerB.x),
                                 std::min(cornerA.y, cornerB.y)},
                            Vec2{std::max(cornerA.x, cornerB.x),
                                 std::max(cornerA.y, cornerB.y)},
                            asset->EditorTint());
                    }
                    ++g_cellsDrawn;
                    continue;
                }

                for (int q = 0; q < kQuadrantCount; ++q) {
                    const auto       quadrant = static_cast<Quadrant>(q);
                    const SpriteRect source =
                        QuadrantSourceRect(slots[quadrant], quadrant,
                                           asset->TileSize(), frame);

                    const bool right = (quadrant == Quadrant::TopRight ||
                                        quadrant == Quadrant::BottomRight);
                    const bool bottom = (quadrant == Quadrant::BottomLeft ||
                                         quadrant == Quadrant::BottomRight);

                    if (snapped) {
                        const float x0 = xs[right ? 1 : 0];
                        const float x1 = xs[right ? 2 : 1];
                        const float y0 = ys[bottom ? 1 : 0];
                        const float y1 = ys[bottom ? 2 : 1];

                        // At a far enough zoom out a quarter rounds to nothing
                        // at all. One pixel rather than zero, so a map viewed
                        // from a distance is a small smudge instead of gone.
                        const Vec2 size{std::max(x1 - x0, 1.0f),
                                        std::max(y1 - y0, 1.0f)};
                        const Vec2 middle{(x0 + x1) * 0.5f, (y0 + y1) * 0.5f};

                        Renderer::DrawSprite(asset->Sheet(), middle, size, 0.0f,
                                             tint, &source);
                    } else {
                        // Rotated: no pixel grid to snap to, so the quarter is
                        // placed by its centre and turned with the map.
                        const Vec2 localCentre{
                            centre.x + (right ? cellSize.x * 0.25f : -cellSize.x * 0.25f),
                            centre.y + (bottom ? -cellSize.y * 0.25f : cellSize.y * 0.25f),
                        };
                        const Vec2 onScreen{
                            cellSize.x * 0.5f * scale.x * camera.Zoom(),
                            cellSize.y * 0.5f * scale.y * camera.Zoom(),
                        };
                        Renderer::DrawSprite(
                            asset->Sheet(),
                            camera.WorldToScreen(world.TransformPoint(localCentre)),
                            onScreen, rotation * kRadToDeg, tint, &source);
                    }
                }

                ++g_cellsDrawn;
            }
        }

        // Everything this chunk holds that did not get drawn: off the edge of
        // the visible range, or - rarely - a cell whose tile asset would not
        // load, which is reported in red in the Inspector.
        g_cellsSkipped += chunk.used - (g_cellsDrawn - drawnBefore);
    }
}

// ---------------------------------------------------------------------------
//  Component plumbing
// ---------------------------------------------------------------------------

TilemapComponent::~TilemapComponent() {
    // Belt and braces: OnDetach is what normally takes the entry out, but a
    // component destroyed without having been detached would leave the render
    // system holding a dangling pointer.
    TilemapRenderSystem::Unregister(*this);
}

void TilemapComponent::OnAttach() { TilemapRenderSystem::Register(*this); }
void TilemapComponent::OnDetach() { TilemapRenderSystem::Unregister(*this); }

// ---------------------------------------------------------------------------
//  TilemapRenderSystem
// ---------------------------------------------------------------------------

void TilemapRenderSystem::Register(TilemapComponent& tilemap) {
    Registry().push_back(&tilemap);
}

void TilemapRenderSystem::Unregister(TilemapComponent& tilemap) {
    std::erase(Registry(), &tilemap);
}

void TilemapRenderSystem::RenderRange(Camera& camera, int minLayer, int maxLayer,
                                      bool editorTints) {
    auto& tilemaps = Registry();
    if (tilemaps.empty()) {
        return;
    }

    // stable_sort, for the same reason SpriteRenderSystem uses it: two maps on
    // one layer keep whatever order they were in, so the picture does not
    // flicker between frames when there is no right answer.
    std::stable_sort(tilemaps.begin(), tilemaps.end(),
                     [](const TilemapComponent* a, const TilemapComponent* b) {
                         return a->Layer() < b->Layer();
                     });

    // Read once per pass rather than once per cell. Animated tiles all want
    // the same instant, and asking the clock inside the loop would let a long
    // frame draw the top of the map at one time and the bottom at another.
    const double seconds = static_cast<double>(Time::ElapsedTime());

    for (const TilemapComponent* tilemap : tilemaps) {
        const int layer = tilemap->Layer();
        if (layer < minLayer || layer >= maxLayer) {
            continue;
        }
        tilemap->Render(camera, seconds, editorTints);
    }
}

void TilemapRenderSystem::CollectLayers(std::vector<int>& outLayers) {
    outLayers.clear();
    for (const TilemapComponent* tilemap : Registry()) {
        outLayers.push_back(tilemap->Layer());
    }
    std::sort(outLayers.begin(), outLayers.end());
    outLayers.erase(std::unique(outLayers.begin(), outLayers.end()),
                    outLayers.end());
}

std::size_t TilemapRenderSystem::Count() { return Registry().size(); }

void TilemapRenderSystem::Clear() { Registry().clear(); }

std::size_t TilemapRenderSystem::CellsDrawn()  { return g_cellsDrawn; }
std::size_t TilemapRenderSystem::CellsSkipped() { return g_cellsSkipped; }

void TilemapRenderSystem::ResetCounters() {
    g_cellsDrawn  = 0;
    g_cellsSkipped = 0;
}

void TilemapRenderSystem::RegisterComponentTypes() {
    ComponentFactory::Register(TilemapComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<TilemapComponent>();
                               });

    // The tilemap COLLIDER is registered here too, rather than beside the five
    // shape colliders in CollisionSystem::RegisterComponentTypes.
    //
    // That is deliberate and it is about which way the dependency points: the
    // tilemap knows about physics, and physics must not come to know about the
    // tilemap. Registering it there would put #include <engine/tilemap/...> in
    // the collision system and tie the two together for no reason beyond where
    // a line of registration code happened to live.
    ComponentFactory::Register(TilemapColliderComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<TilemapColliderComponent>();
                               });
}

} // namespace eng
