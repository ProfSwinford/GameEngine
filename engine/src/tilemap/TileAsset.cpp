#include <engine/tilemap/TileAsset.h>

#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>
#include <engine/resource/ImportSettings.h>   // to refuse a sliced spritesheet
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

const char* TileArtName(TileArt art) {
    switch (art) {
        case TileArt::AutoTile: return "AutoTile";
        case TileArt::Image:    return "Image";
    }
    return "AutoTile";
}

int TileSizeFor(TileArt art, int imageHeight) {
    return (art == TileArt::Image) ? imageHeight : imageHeight / kSheetBands;
}

TileArtShape CheckTileArtShape(TileArt art, int tileSize, int imageWidth,
                               int imageHeight, int statedFrames,
                               std::string_view artPath) {
    TileArtShape out;

    const std::string key  = (art == TileArt::Image) ? "image" : "sheet";
    const std::string path(artPath);
    const std::string size =
        std::to_string(imageWidth) + "x" + std::to_string(imageHeight);

    // A GUARD RATHER THAN A CHECK, and not the duplicate it looks like.
    // Deserialize refuses a non-positive tileSize before it ever gets here,
    // in the same words; this is for a direct caller, and it is load-bearing
    // because the frame count below divides by it.
    if (tileSize <= 0) {
        out.problem = "a tile size must be a positive number of pixels (got " +
                      std::to_string(tileSize) + ")";
        return out;
    }

    // ---- the height says whether this really is what it claims to be ------
    const int wantHeight =
        (art == TileArt::Image) ? tileSize : tileSize * kSheetBands;
    if (imageHeight != wantHeight) {
        out.problem = "TileAsset." + key + " '" + path + "' is " + size +
                      ", but art for a " + std::to_string(tileSize) +
                      "px tile must be exactly " + std::to_string(wantHeight) +
                      "px tall" +
                      (art == TileArt::Image
                           ? " - one tile, drawn whole."
                           : " - five bands of one tile each. See AutoTile.h. If "
                             "this is an ordinary picture rather than a five-band "
                             "sheet, it wants \"image\" and not \"sheet\".");
        return out;
    }

    // ---- and the width says how many frames there are ---------------------
    out.frames = (statedFrames > 0) ? statedFrames
                                    : std::max(imageWidth / tileSize, 1);

    if (imageWidth < tileSize * out.frames) {
        if (art == TileArt::Image && statedFrames <= 0) {
            // THE COMMONEST WAY TO MEET THIS, and the generic message below is
            // true and useless for it: the reader is not thinking in frames at
            // all, they have a picture that is not square.
            out.problem = "TileAsset.image '" + path + "' is " + size +
                          ", and a tile is square. An image tile must be one square "
                          "picture, or a sideways strip of them - so this one would "
                          "have to be " + std::to_string(imageHeight) +
                          "px wide, or a multiple of it. Crop it square, or state "
                          "\"tileSize\" if you meant something else.";
        } else {
            out.problem = "TileAsset." + key + " '" + path + "' is only " +
                          std::to_string(imageWidth) + "px wide, which is not enough "
                          "for " + std::to_string(out.frames) + " frame(s) of " +
                          std::to_string(tileSize) + "px";
        }
        return out;
    }

    if (imageWidth % tileSize != 0) {
        out.note = "tile art '" + path + "' is " + std::to_string(imageWidth) +
                   "px wide, which is not a whole number of " +
                   std::to_string(tileSize) + "px tiles; the rightmost " +
                   std::to_string(imageWidth % tileSize) +
                   "px will never be drawn";
    }
    return out;
}

bool TileAsset::Deserialize(const Json& node, std::string& outError) {
    if (!node.is_object()) {
        outError = "a tile asset must be a JSON object";
        return false;
    }

    // Read FIRST, because it decides whether a sheet is required at all.
    m_connector = ReadBool(node, "connector", false, "TileAsset");

    // ---- which kind of art, decided by which key is present ---------------
    //
    // THE KEY IS THE KIND. See the note at the top of TileAsset.h for why
    // there is no "kind" field to disagree with it.
    const bool hasSheetKey = HasKey(node, "sheet");
    const bool hasImageKey = HasKey(node, "image");

    if (hasSheetKey && hasImageKey) {
        outError = "TileAsset names both \"sheet\" and \"image\", and they are the "
                   "two kinds of art a tile can have - not two settings. A "
                   "\"sheet\" is five bands assembled from its neighbours; an "
                   "\"image\" is one picture drawn whole. Keep the one you meant.";
        return false;
    }

    m_art       = hasImageKey ? TileArt::Image : TileArt::AutoTile;
    m_artPath = ReadString(node, hasImageKey ? "image" : "sheet", "", "TileAsset");

    if (m_artPath.empty() && !m_connector) {
        // BOTH KEYS NAMED, because somebody writing their first .otile does
        // not yet know there are two kinds, and "sheet is required" sent them
        // to point it at whatever picture they had.
        outError = "a tile needs art: either \"sheet\", the five-band sheet that is "
                   "assembled from its neighbours (see AutoTile.h), or \"image\", "
                   "one picture drawn whole in every cell. An ordinary picture is "
                   "\"image\". (Only a \"connector\" tile may omit both, because a "
                   "connector is invisible.)";
        return false;
    }

    // A connector draws nothing, so it needs a tile size and no art at all.
    if (m_artPath.empty()) {
        m_tileSize   = std::max(ReadInt(node, "tileSize", 16, "TileAsset"), 2);
        m_frameCount = 1;
        m_frameMode  = TileFrameMode::Single;
        m_collision  = TileCollision::None;
        return DeserializeConnections(node, outError);
    }

    m_artTexture = ResourceManager::LoadTexture(m_artPath);

    const bool sheetMissing = (!m_artTexture || m_artTexture->isPlaceholder);
    const int  sheetWidth   = m_artTexture ? m_artTexture->width : 0;
    const int  sheetHeight  = m_artTexture ? m_artTexture->height : 0;

    // ---- tile size: derived unless stated, and checked when stated ---------
    const bool tileSizeGiven = HasKey(node, "tileSize");
    if (tileSizeGiven) {
        m_tileSize = ReadInt(node, "tileSize", 0, "TileAsset");
    } else if (sheetMissing) {
        // Nothing to measure and nothing stated. Guessing would produce a map
        // whose cells are the wrong size, which looks like a different bug.
        outError = std::string("TileAsset.") +
                   (m_art == TileArt::Image ? "image" : "sheet") + " '" +
                   m_artPath +
                   "' could not be loaded, so tileSize cannot be worked out from "
                   "it. Fix the path, or state \"tileSize\" explicitly.";
        return false;
    } else {
        m_tileSize = TileSizeFor(m_art, sheetHeight);
    }

    if (m_tileSize <= 0) {
        outError = "TileAsset.tileSize must be a positive number of pixels (got " +
                   std::to_string(m_tileSize) + ")";
        return false;
    }

    // An AUTO-TILE is drawn as four quarters, so an odd size would split a
    // pixel and leave a one-pixel seam down the middle of every tile in the
    // game. AN IMAGE IS DRAWN WHOLE and has no such problem, so a 15px image
    // tile is perfectly legal - the rule follows the reason for it rather than
    // applying to tiles in general.
    if (m_art == TileArt::AutoTile && m_tileSize % 2 != 0) {
        outError = "TileAsset.tileSize must be EVEN for a \"sheet\" (got " +
                   std::to_string(m_tileSize) +
                   "): an auto-tile is drawn as four half-size quarters, and an "
                   "odd size cannot be halved without losing a pixel. An "
                   "\"image\" tile is drawn whole and may be any size.";
        return false;
    }

    // ---- the frame count and the shape, which are one question ------------
    //
    // ONE FUNCTION ANSWERS BOTH, because the number of frames IS a reading of
    // the width and the shape check is the other half of the same reading.
    // It lives outside this class so that it can be tested without an image -
    // see CheckTileArtShape in TileAsset.h.
    const int statedFrames =
        HasKey(node, "frames") ? ReadInt(node, "frames", 1, "TileAsset") : 0;

    if (sheetMissing) {
        // A WARNING and not an error, because ResourceManager has already
        // reported the real failure at error level. Saying it twice would
        // make one broken path look like two broken things; what this adds
        // is the tile-specific consequence.
        ENGINE_LOG_WARN(Channels::kTilemap,
                        "tile art '{}' could not be loaded; tiles using it will "
                        "draw as the magenta placeholder at {}px",
                        m_artPath, m_tileSize);
        m_frameCount = std::max(statedFrames, 1);
    } else {
        const TileArtShape shape = CheckTileArtShape(
            m_art, m_tileSize, sheetWidth, sheetHeight, statedFrames, m_artPath);
        if (!shape.Ok()) {
            outError = shape.problem;
            return false;
        }
        m_frameCount = shape.frames;
        if (!shape.note.empty()) {
            ENGINE_LOG_WARN(Channels::kTilemap, "{}", shape.note);
        }

        // ======================================================================
        //  A SPRITESHEET IS NOT A TILE SHEET, and no measurement can tell them
        //  apart - they are the same shape. This is the one signal that can.
        //
        //  The height check above derives the tile size as height / 5 and then
        //  asks whether the height is five times it, which every image whose
        //  height is a multiple of ten passes. So a 48x80 character sheet was
        //  accepted as a 16px tile with three frames, and drawn as scrambled
        //  quarters - a bug that looks like a renderer fault and is not one.
        //
        //  SLICING AN IMAGE IS A STATEMENT THAT IT IS A SPRITESHEET. It is
        //  written in the sidecar, by somebody who meant it, which is worth
        //  more than anything guessable from the dimensions. See
        //  resource/ImportSettings.h.
        //
        //  REFUSED rather than warned, because a warning is dropped entirely
        //  when the log threshold is Error - and this is precisely the case
        //  where the result on screen is nonsense rather than nothing.
        // ======================================================================
        if (m_art == TileArt::AutoTile) {
            const TextureImport imported = ImportLibrary::ForTexture(m_artPath);
            if (!imported.sheet.frames.empty()) {
                outError = "TileAsset.sheet '" + m_artPath + "' has been sliced "
                           "into " + std::to_string(imported.sheet.frames.size()) +
                           " frame(s), so it is a spritesheet rather than a "
                           "five-band tile sheet - and as one it would draw as "
                           "scrambled quarters. Use \"image\" with one of its "
                           "frames (e.g. \"" + m_artPath + "#" +
                           imported.sheet.frames.front().name +
                           "\"), or remove the slicing in the Inspector if this "
                           "really is a tile sheet.";
                return false;
            }
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
                        "tile '{}' asks for frameMode \"{}\" but its art '{}' "
                        "holds only one frame, so it will not vary. Frames go "
                        "SIDEWAYS: widen the image to a multiple of {}px.",
                        m_path.empty() ? m_artPath : m_path,
                        TileFrameModeName(m_frameMode), m_artPath, m_tileSize);
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
                        m_path.empty() ? m_artPath : m_path);
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
    if (!m_artPath.empty()) {
        // THE KEY CARRIES THE KIND, so writing the right one is the whole of
        // saving it. See TileAsset.h.
        out[m_art == TileArt::Image ? "image" : "sheet"] = m_artPath;
    }

    // tileSize and frames are written out even though they are derived, so the
    // file says what the engine concluded. A sheet later resized by accident
    // then fails the check in Deserialize instead of silently re-deriving a
    // different tile size and scrambling every map that uses it.
    out["tileSize"] = m_tileSize;

    if (!m_artPath.empty()) {
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
