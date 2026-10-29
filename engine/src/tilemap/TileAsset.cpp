#include <engine/tilemap/TileAsset.h>

#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>
#include <engine/resource/ResourceManager.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

namespace eng {

// ---------------------------------------------------------------------------
//  Names, and reading them back
// ---------------------------------------------------------------------------

const char* TileFrameModeName(TileFrameMode mode) {
    switch (mode) {
        case TileFrameMode::Single:    return "Single";
        case TileFrameMode::Animation: return "Animation";
        case TileFrameMode::Random:    return "Random";
    }
    return "Single";
}

TileFrameMode ParseTileFrameMode(std::string_view name, bool& outRecognized) {
    outRecognized = true;
    if (name == "Single")    { return TileFrameMode::Single; }
    if (name == "Animation") { return TileFrameMode::Animation; }
    if (name == "Random")    { return TileFrameMode::Random; }
    outRecognized = false;
    return TileFrameMode::Single;
}

const char* TileCollisionName(TileCollision collision) {
    switch (collision) {
        case TileCollision::None: return "None";
        case TileCollision::Grid: return "Grid";
    }
    return "Grid";
}

TileCollision ParseTileCollision(std::string_view name, bool& outRecognized) {
    outRecognized = true;
    if (name == "None") { return TileCollision::None; }
    if (name == "Grid") { return TileCollision::Grid; }
    outRecognized = false;
    return TileCollision::Grid;
}

// ---------------------------------------------------------------------------
//  Per-cell randomness
// ---------------------------------------------------------------------------

namespace {

// A plain integer hash of a cell position.
//
// DELIBERATELY NOT std::hash. Its results are not specified by the standard
// and differ between implementations, so a map full of randomly varied stones
// would arrange itself differently on another compiler - a difference that
// would show up as "the level looks wrong on my machine" and take a long time
// to trace back to here.
std::uint32_t CellHash(int cellX, int cellY, std::uint32_t salt) {
    std::uint32_t h = static_cast<std::uint32_t>(cellX) * 0x9E3779B1u;
    h ^= static_cast<std::uint32_t>(cellY) * 0x85EBCA77u;
    h ^= salt * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2545F491u;
    h ^= h >> 13;
    return h;
}

// The same hash, as a number from 0 up to but not including 1.
float CellHash01(int cellX, int cellY, std::uint32_t salt) {
    return static_cast<float>(CellHash(cellX, cellY, salt) >> 8) /
           static_cast<float>(1u << 24);
}

} // namespace

int TileAsset::FrameForCell(int cellX, int cellY, double seconds) const {
    if (m_frameCount <= 1) {
        return 0;
    }

    switch (m_frameMode) {
        case TileFrameMode::Single:
            return 0;

        case TileFrameMode::Random:
            return static_cast<int>(CellHash(cellX, cellY, 1u) %
                                    static_cast<std::uint32_t>(m_frameCount));

        case TileFrameMode::Animation: {
            // A speed and a starting point PER CELL, so a field of water does
            // not pulse in unison. Moving together is the single thing that
            // makes animated tiles read as a grid rather than as a surface.
            const float pick  = CellHash01(cellX, cellY, 2u);
            const float speed = m_animationMinSpeed +
                                (m_animationMaxSpeed - m_animationMinSpeed) * pick;
            const float offset = CellHash01(cellX, cellY, 3u) *
                                 static_cast<float>(m_frameCount);

            const double advanced = seconds * static_cast<double>(speed) +
                                    static_cast<double>(offset);

            // std::fmod keeps the sign of its left operand, so a negative time
            // would give a negative frame and index out of the sheet. The
            // second wrap costs nothing and removes the possibility.
            double wrapped = std::fmod(advanced, static_cast<double>(m_frameCount));
            if (wrapped < 0.0) {
                wrapped += static_cast<double>(m_frameCount);
            }
            const int frame = static_cast<int>(wrapped);
            return std::clamp(frame, 0, m_frameCount - 1);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
//  Connecting
// ---------------------------------------------------------------------------

bool TileAsset::ConnectsToAsset(const TileAsset& other) const {
    // The same material always joins itself. A pointer comparison, because
    // TileLibrary hands out one instance per file.
    if (&other == this) {
        return true;
    }

    // A CONNECTOR JOINS EVERYTHING. That is its whole purpose - it exists to be
    // seen by its neighbours and nothing else. See the note in the header.
    if (other.IsConnector()) {
        return true;
    }

    // Exact path match. Not case-folded and not normalized: the paths in a
    // .otile are the paths written in the scene file, and quietly accepting a
    // near-miss would mean the editor's own writing of a path and a hand-typed
    // one could behave differently.
    return std::find(m_connectsTo.begin(), m_connectsTo.end(), other.Path()) !=
           m_connectsTo.end();
}

// ---------------------------------------------------------------------------
//  Reading a .otile
// ---------------------------------------------------------------------------

bool TileAsset::Deserialize(const Json& node, std::string& outError) {
    if (!node.is_object()) {
        outError = "a tile asset must be a JSON object";
        return false;
    }

    // Read FIRST, because it decides whether a sheet is required at all.
    m_connector = ReadBool(node, "connector", false, "TileAsset");

    m_sheetPath = ReadString(node, "sheet", "", "TileAsset");
    if (m_sheetPath.empty() && !m_connector) {
        outError = "TileAsset.sheet is required: the five-band tile sheet to draw "
                   "from. (Only a \"connector\" tile may omit it, because a "
                   "connector is invisible.)";
        return false;
    }

    // A connector draws nothing, so it needs a tile size and no art at all.
    if (m_sheetPath.empty()) {
        m_tileSize   = std::max(ReadInt(node, "tileSize", 16, "TileAsset"), 2);
        m_frameCount = 1;
        m_frameMode  = TileFrameMode::Single;
        m_collision  = TileCollision::None;
        return DeserializeConnections(node, outError);
    }

    m_sheet = ResourceManager::LoadTexture(m_sheetPath);

    const bool sheetMissing = (!m_sheet || m_sheet->isPlaceholder);
    const int  sheetWidth   = m_sheet ? m_sheet->width : 0;
    const int  sheetHeight  = m_sheet ? m_sheet->height : 0;

    // ---- tile size: derived unless stated, and checked when stated ---------
    const bool tileSizeGiven = HasKey(node, "tileSize");
    if (tileSizeGiven) {
        m_tileSize = ReadInt(node, "tileSize", 0, "TileAsset");
    } else if (sheetMissing) {
        // Nothing to measure and nothing stated. Guessing would produce a map
        // whose cells are the wrong size, which looks like a different bug.
        outError = "TileAsset.sheet '" + m_sheetPath +
                   "' could not be loaded, so tileSize cannot be worked out from "
                   "it. Fix the path, or state \"tileSize\" explicitly.";
        return false;
    } else {
        m_tileSize = sheetHeight / kSheetBands;
    }

    if (m_tileSize <= 0) {
        outError = "TileAsset.tileSize must be a positive number of pixels (got " +
                   std::to_string(m_tileSize) + ")";
        return false;
    }

    // A tile is drawn as four quarters, so an odd size would split a pixel and
    // leave a one-pixel seam down the middle of every tile in the game.
    if (m_tileSize % 2 != 0) {
        outError = "TileAsset.tileSize must be EVEN (got " +
                   std::to_string(m_tileSize) +
                   "): a tile is drawn as four half-size quarters, and an odd "
                   "size cannot be halved without losing a pixel";
        return false;
    }

    // ---- frame count: derived unless stated --------------------------------
    const bool framesGiven = HasKey(node, "frames");
    if (framesGiven) {
        m_frameCount = ReadInt(node, "frames", 1, "TileAsset");
    } else if (sheetMissing) {
        m_frameCount = 1;
    } else {
        m_frameCount = sheetWidth / m_tileSize;
    }
    m_frameCount = std::max(m_frameCount, 1);

    // ---- now check the sheet is actually the shape we have been told -------
    if (sheetMissing) {
        // A WARNING and not an error, because ResourceManager has already
        // reported the real failure at error level. Saying it twice would
        // make one broken path look like two broken things; what this adds
        // is the tile-specific consequence.
        ENGINE_LOG_WARN(Channels::kTilemap,
                        "tile sheet '{}' could not be loaded; tiles using it will "
                        "draw as the magenta placeholder at {}px",
                        m_sheetPath, m_tileSize);
    } else {
        if (sheetHeight != m_tileSize * kSheetBands) {
            outError = "TileAsset.sheet '" + m_sheetPath + "' is " +
                       std::to_string(sheetWidth) + "x" + std::to_string(sheetHeight) +
                       ", but a sheet for a " + std::to_string(m_tileSize) +
                       "px tile must be exactly " +
                       std::to_string(m_tileSize * kSheetBands) +
                       "px tall - five bands of one tile each. See AutoTile.h.";
            return false;
        }
        if (sheetWidth < m_tileSize * m_frameCount) {
            outError = "TileAsset.sheet '" + m_sheetPath + "' is only " +
                       std::to_string(sheetWidth) + "px wide, which is not enough "
                       "for " + std::to_string(m_frameCount) + " frame(s) of " +
                       std::to_string(m_tileSize) + "px";
            return false;
        }
        if (sheetWidth % m_tileSize != 0) {
            ENGINE_LOG_WARN(Channels::kTilemap,
                            "tile sheet '{}' is {}px wide, which is not a whole "
                            "number of {}px tiles; the rightmost {}px will never "
                            "be drawn",
                            m_sheetPath, sheetWidth, m_tileSize,
                            sheetWidth % m_tileSize);
        }
    }

    // ---- frames: how to use them -------------------------------------------
    const std::string frameModeName =
        ReadString(node, "frameMode", TileFrameModeName(m_frameMode), "TileAsset");
    bool recognized = false;
    m_frameMode     = ParseTileFrameMode(frameModeName, recognized);
    if (!recognized) {
        outError = "TileAsset.frameMode must be \"Single\", \"Animation\" or "
                   "\"Random\" (got \"" + frameModeName + "\")";
        return false;
    }

    m_animationMinSpeed = ReadFloat(node, "animationMinSpeed", 4.0f, "TileAsset");
    m_animationMaxSpeed = ReadFloat(node, "animationMaxSpeed",
                                    m_animationMinSpeed, "TileAsset");
    if (m_animationMaxSpeed < m_animationMinSpeed) {
        std::swap(m_animationMinSpeed, m_animationMaxSpeed);
    }

    if (m_frameMode != TileFrameMode::Single && m_frameCount <= 1) {
        ENGINE_LOG_WARN(Channels::kTilemap,
                        "tile '{}' asks for frameMode \"{}\" but its sheet '{}' "
                        "holds only one frame, so it will not vary. Frames go "
                        "SIDEWAYS: widen the sheet to a multiple of {}px.",
                        m_path.empty() ? m_sheetPath : m_path,
                        TileFrameModeName(m_frameMode), m_sheetPath, m_tileSize);
    }

    // ---- collision ---------------------------------------------------------
    const std::string collisionName =
        ReadString(node, "collision", TileCollisionName(m_collision), "TileAsset");
    m_collision = ParseTileCollision(collisionName, recognized);
    if (!recognized) {
        outError = "TileAsset.collision must be \"None\" or \"Grid\" (got \"" +
                   collisionName + "\")";
        return false;
    }

    return DeserializeConnections(node, outError);
}

// The part a connector shares with an ordinary tile. Everything above this is
// about art, and a connector has none.
bool TileAsset::DeserializeConnections(const Json& node, std::string& outError) {
    m_connectsTo.clear();
    if (HasKey(node, "connectsTo")) {
        const Json& list = node.at("connectsTo");
        if (!list.is_array()) {
            outError = "TileAsset.connectsTo must be a list of .otile paths";
            return false;
        }
        for (const Json& entry : list) {
            if (!entry.is_string()) {
                outError = "TileAsset.connectsTo entries must each be text";
                return false;
            }
            m_connectsTo.push_back(entry.get<std::string>());
        }
    }

    m_editorTint = Color{0, 0, 0, 0};
    if (HasKey(node, "editorTint")) {
        const Json& tint = node.at("editorTint");
        if (!tint.is_array() || tint.size() != 4) {
            outError = "TileAsset.editorTint must be four numbers, [r, g, b, a]";
            return false;
        }
        unsigned char channels[4]{};
        for (std::size_t i = 0; i < 4; ++i) {
            if (!tint[i].is_number()) {
                outError = "TileAsset.editorTint entries must each be a number 0-255";
                return false;
            }
            channels[i] = static_cast<unsigned char>(
                std::clamp(tint[i].get<int>(), 0, 255));
        }
        m_editorTint = Color{channels[0], channels[1], channels[2], channels[3]};
    }

    // A connector is invisible and has no presence; giving it a collider would
    // put an invisible wall along the very seam it was added to hide.
    if (m_connector && m_collision != TileCollision::None) {
        ENGINE_LOG_WARN(Channels::kTilemap,
                        "tile '{}' is a connector, so its \"collision\" setting is "
                        "ignored: a connector exists only to be seen by its "
                        "neighbours, and a solid one would be an invisible wall",
                        m_path.empty() ? m_sheetPath : m_path);
        m_collision = TileCollision::None;
    }

    // Nothing marks a connector out on screen, so without a tint it is
    // genuinely invisible in the editor too - and an invisible thing that
    // changes how its neighbours look is a bad half-hour. Default it to a
    // faint magenta rather than leaving it unfindable.
    if (m_connector && m_editorTint.a == 0) {
        m_editorTint = Color{245, 66, 233, 90};
    }

    return true;
}

bool TileAsset::Serialize(Json& out) const {
    out = Json::object();

    if (m_connector) {
        out["connector"] = true;
    }
    if (!m_sheetPath.empty()) {
        out["sheet"] = m_sheetPath;
    }

    // tileSize and frames are written out even though they are derived, so the
    // file says what the engine concluded. A sheet later resized by accident
    // then fails the check in Deserialize instead of silently re-deriving a
    // different tile size and scrambling every map that uses it.
    out["tileSize"] = m_tileSize;

    if (!m_sheetPath.empty()) {
        out["frames"]    = m_frameCount;
        out["frameMode"] = TileFrameModeName(m_frameMode);
    }

    if (m_frameMode == TileFrameMode::Animation) {
        out["animationMinSpeed"] = m_animationMinSpeed;
        out["animationMaxSpeed"] = m_animationMaxSpeed;
    }
    if (!m_connectsTo.empty()) {
        out["connectsTo"] = m_connectsTo;
    }

    out["collision"] = TileCollisionName(m_collision);

    if (m_editorTint.a != 0) {
        out["editorTint"] = Json::array({m_editorTint.r, m_editorTint.g,
                                         m_editorTint.b, m_editorTint.a});
    }
    return true;
}

// ---------------------------------------------------------------------------
//  TileLibrary
// ---------------------------------------------------------------------------

namespace {

// std::map, not unordered_map: All() wants a stable order so the editor's
// palette does not reshuffle itself between runs, and the number of tile
// assets in a project is measured in dozens.
//
// unique_ptr because TileAsset is not copyable and pointers to the stored
// assets are handed out and kept - growing the map must not move them.
std::map<std::string, std::unique_ptr<TileAsset>, std::less<>>& Assets() {
    // In a function, not at file scope: the engine DLL exports functions and
    // NOT data, so state lives inside a function. See engine/CMakeLists.txt.
    static std::map<std::string, std::unique_ptr<TileAsset>, std::less<>> assets;
    return assets;
}

// Reads and parses the file, leaving the parsed object in `outNode`. Split out
// from deserializing so Reload can parse once and then apply the same node
// twice - see the note there.
bool ParseAssetFile(std::string_view virtualPath, Json& outNode,
                    std::string& outError) {
    std::string text;
    if (!FileSystem::ReadTextFile(virtualPath, text, outError)) {
        return false;
    }

    outNode = ParseJson(text, outError);
    return outError.empty();
}

bool ReadAssetFile(std::string_view virtualPath, TileAsset& into,
                   std::string& outError) {
    Json node;
    if (!ParseAssetFile(virtualPath, node, outError)) {
        return false;
    }

    into.SetPath(virtualPath);
    return into.Deserialize(node, outError);
}

} // namespace

const TileAsset* TileLibrary::Load(std::string_view virtualPath) {
    if (virtualPath.empty()) {
        return nullptr;
    }

    auto& assets = Assets();
    if (const auto it = assets.find(virtualPath); it != assets.end()) {
        return it->second.get();
    }

    auto        asset = std::make_unique<TileAsset>();
    std::string error;
    if (!ReadAssetFile(virtualPath, *asset, error)) {
        ENGINE_LOG_ERROR(Channels::kTilemap, "tile asset '{}' failed to load: {}",
                         virtualPath, error);
        return nullptr;
    }

    // INSERTED BEFORE ANYTHING ELSE LOOKS AT IT. Two tiles that name each other
    // in connectsTo are perfectly ordinary, and resolving those names during
    // the load would recurse between them forever. Nothing here follows them -
    // ConnectsToAsset compares paths, so the names are never chased - but a
    // future reader should know the ordering is load-bearing.
    const std::string key(virtualPath);
    TileAsset*        raw = asset.get();
    assets.emplace(key, std::move(asset));

    ENGINE_LOG_INFO(Channels::kTilemap,
                    "loaded tile '{}': {}px, {} frame(s), {}, collision {}{}",
                    virtualPath, raw->TileSize(), raw->FrameCount(),
                    TileFrameModeName(raw->FrameMode()),
                    TileCollisionName(raw->Collision()),
                    raw->IsConnector() ? ", connector" : "");
    return raw;
}

const TileAsset* TileLibrary::Find(std::string_view virtualPath) {
    auto&      assets = Assets();
    const auto it     = assets.find(virtualPath);
    return (it == assets.end()) ? nullptr : it->second.get();
}

bool TileLibrary::Reload(std::string_view virtualPath, std::string& outError) {
    auto&      assets = Assets();
    const auto it     = assets.find(virtualPath);
    if (it == assets.end()) {
        outError = "tile asset '" + std::string(virtualPath) + "' is not loaded";
        return false;
    }

    Json node;
    if (!ParseAssetFile(virtualPath, node, outError)) {
        return false;
    }

    // DESERIALIZED INTO A SCRATCH ASSET FIRST, and only then into the live one.
    //
    // Deserialize fails part of the way through on a bad field, and the live
    // asset is the one every map in the scene is pointing at - so applying a
    // file that is being typed into would leave maps drawing from a tile that
    // is half old and half new. While a .otile is being edited, "has an error
    // in it" is its usual state.
    TileAsset probe;
    probe.SetPath(virtualPath);
    if (!probe.Deserialize(node, outError)) {
        return false;
    }

    return it->second->Deserialize(node, outError);
}

std::size_t TileLibrary::Count() { return Assets().size(); }

void TileLibrary::Clear() {
    auto& assets = Assets();
    if (!assets.empty()) {
        ENGINE_LOG_INFO(Channels::kTilemap, "unloading {} tile asset(s)",
                        assets.size());
    }
    assets.clear();
}

std::vector<const TileAsset*> TileLibrary::All() {
    std::vector<const TileAsset*> flat;
    flat.reserve(Assets().size());
    for (const auto& [path, asset] : Assets()) {
        flat.push_back(asset.get());
    }
    return flat;
}

} // namespace eng
