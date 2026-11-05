// ============================================================================
//  ImportSettings.cpp - reading and writing .oimport files. See the header.
//
//  ==========================================================================
//  THE READ PATH DOES NOT ASK WHETHER THE FILE EXISTS, AND THAT IS THE ONE
//  THING HERE WORTH READING BEFORE CHANGING ANYTHING.
//
//  It tries the read and treats every failure as "no settings". The obvious
//  alternative - ask Exists, then read - would be WRONG IN A SHIPPED GAME.
//
//  FileSystem::Exists answers from the ARCHIVE INDEX when an archive is
//  mounted (see the guard at the top of FileSystem::Exists), which is an
//  in-memory table that never opens the .bin; FileSystem::ReadTextFile has to
//  open it, and so has failure modes Exists cannot see. The two therefore
//  agree about presence and NOT about readability. Gating on Exists buys
//  nothing and risks a sidecar that every tool reports as present and nothing
//  can read.
//
//  So Exists is used here for exactly one thing - deciding whether a FAILED
//  read is worth a message - and never to decide whether to read.
//
//  ==========================================================================
//  WHY THERE IS A CACHE AT ALL, given that an image is loaded once.
//
//  Because the Inspector asks EVERY FRAME. It draws the selected asset's
//  settings sixty times a second, and each of those is a question this file
//  answers. Without the cache that is a file read per frame for as long as
//  somebody leaves an image selected - the sort of cost that makes a tool feel
//  slow for a reason nobody can point at.
//
//  THE ABSENCE OF A SIDECAR IS CACHED TOO, as the defaults. That is what stops
//  the common case - the overwhelming majority of images, which have no
//  settings at all - from being the one case that still hits the disk every
//  frame.
//
//  Save, Revert and FollowAsset all keep the cache in step, so every change
//  made THROUGH the editor is live. An edit made from outside waits for
//  Clear(); see the note on it in the header.
// ============================================================================

#include <engine/core/Json.h>
#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>
#include <engine/resource/ImportSettings.h>

#include <numeric>   // gcd, for guessing a cell size
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eng {
namespace {

// asset path -> the settings resolved for it, defaults included. See the file
// header on why a miss is cached rather than retried.
std::unordered_map<std::string, TextureImport> g_textures;
std::unordered_map<std::string, FontImport>    g_fonts;

// See ImportLibrary::Generation. ONE is the starting value rather than zero,
// so that zero can mean "never asked" to whoever is remembering it.
std::uint64_t g_generation = 1;

// Called beside every write to the two caches above, and nowhere else.
void Touch() {
    ++g_generation;
}

// The one field name in the file, spelled once.
constexpr const char* kFilterKey = "filter";

// ---------------------------------------------------------------------------
//  Reads a sidecar, or reports that there was nothing usable in it.
//
//  Returns false for "use the defaults", which covers a missing file, an
//  unreadable one and one that is not JSON. `outDocument` is only meaningful
//  when it returns true.
// ---------------------------------------------------------------------------
bool ReadSidecar(std::string_view assetPath, Json& outDocument) {
    const std::string sidecar = ImportLibrary::SidecarPath(assetPath);

    std::string text;
    std::string error;
    if (!FileSystem::ReadTextFile(sidecar, text, error)) {
        // SILENT WHEN THERE IS SIMPLY NO FILE, which is the normal case for
        // almost every asset in a project and would otherwise be one warning
        // per image per run - a log nobody reads.
        //
        // Exists is asked ONLY here, to tell "there is nothing to read" apart
        // from "there is something and it would not read": a permission
        // problem, a file open elsewhere, a damaged archive. That second one is
        // worth saying out loud, because settings somebody wrote are not being
        // applied and nothing else would ever mention it.
        if (FileSystem::Exists(sidecar)) {
            ENGINE_LOG_WARN(Channels::kResource,
                            "'{}' is there but could not be read ({}), so '{}' is being "
                            "imported with the default settings",
                            sidecar, error, assetPath);
        }
        return false;
    }

    outDocument = ParseJson(text, error);
    if (!error.empty()) {
        // AT WARN AND NAMING THE FILE, because this is a typo in something a
        // person edited and this line is the only way they find out.
        ENGINE_LOG_WARN(Channels::kResource,
                        "'{}' is not valid JSON ({}), so '{}' is being imported with "
                        "the default settings",
                        sidecar, error, assetPath);
        return false;
    }

    if (!outDocument.is_object()) {
        ENGINE_LOG_WARN(Channels::kResource,
                        "'{}' does not hold a set of settings, so '{}' is being "
                        "imported with the default settings",
                        sidecar, assetPath);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Reads the one field both kinds of settings share.
//
//  `fallback` is what THIS KIND of asset defaults to, which is not the same
//  for a texture and a font - see the header. Passing it in rather than
//  letting ParseTextureFilter choose is what keeps a font's default from
//  quietly becoming a texture's.
// ---------------------------------------------------------------------------
TextureFilter ReadFilter(const Json& document, const std::string& sidecar,
                         TextureFilter fallback) {
    if (!HasKey(document, kFilterKey)) {
        return fallback;
    }

    // ReadString warns by itself when the key is there and holds something
    // that is not text, and hands back the fallback - which here is the empty
    // string, and so is a name no filter has. That lands in the message below,
    // which is the right place for it.
    const std::string name = ReadString(document, kFilterKey, "", sidecar);

    bool                recognized = false;
    const TextureFilter filter     = ParseTextureFilter(name, recognized, fallback);
    if (!recognized) {
        // THE ACCEPTED NAMES ARE IN THE MESSAGE. A warning that says a value is
        // wrong without saying what would be right is a warning that sends
        // somebody to read the engine's source. Same shape as the refusal in
        // TileAsset::Deserialize, which this deliberately does not copy in its
        // other half: that one fails, and this one carries on. The header says
        // why.
        ENGINE_LOG_WARN(Channels::kResource,
                        "'{}' asks for the scaling mode '{}', which this engine does not "
                        "have - it must be \"{}\" or \"{}\". Using \"{}\".",
                        sidecar, name, TextureFilterName(TextureFilter::Nearest),
                        TextureFilterName(TextureFilter::Linear),
                        TextureFilterName(fallback));
    }
    return filter;
}

// ---------------------------------------------------------------------------
//  The "sheet" block.
//
//  EVERY BAD ENTRY IS SKIPPED AND NAMED, and the rest of the file is still
//  used - the same choice the filter makes, for the same reason: a sheet with
//  one malformed frame is still a sheet, and the worst thing this layer could
//  do is turn a typo into a level that will not load.
// ---------------------------------------------------------------------------
void ReadSheet(const Json& node, const std::string& sidecar, SheetSlice& out) {
    if (!HasKey(node, "sheet")) {
        return;
    }
    const Json& sheet = node["sheet"];
    if (!sheet.is_object()) {
        ENGINE_LOG_WARN(Channels::kResource,
                        "'{}' has a \"sheet\" that is not a set of settings; it is "
                        "being ignored", sidecar);
        return;
    }

    out.cellWidth  = ReadInt(sheet, "cellWidth", 0, sidecar);
    out.cellHeight = ReadInt(sheet, "cellHeight", 0, sidecar);
    out.margin     = ReadInt(sheet, "margin", 0, sidecar);
    out.spacing    = ReadInt(sheet, "spacing", 0, sidecar);

    // ---- frames -----------------------------------------------------------
    if (HasKey(sheet, "frames") && sheet["frames"].is_array()) {
        for (const Json& entry : sheet["frames"]) {
            if (!entry.is_object()) {
                continue;
            }
            SheetFrame frame;
            frame.name = ReadString(entry, "name", "", sidecar);
            frame.x    = ReadInt(entry, "x", 0, sidecar);
            frame.y    = ReadInt(entry, "y", 0, sidecar);
            frame.w    = ReadInt(entry, "w", 0, sidecar);
            frame.h    = ReadInt(entry, "h", 0, sidecar);

            std::string reason;
            if (!IsLegalSubAssetName(frame.name, reason)) {
                ENGINE_LOG_WARN(Channels::kResource,
                                "'{}' has a frame this engine cannot name ({}); it is "
                                "being skipped", sidecar, reason);
                continue;
            }
            // A FRAME WITH NO AREA IS NOT A FRAME. Checked here rather than at
            // the point the pixels are copied, so a hand-edited zero cannot
            // reach the slicing arithmetic at all.
            if (frame.w <= 0 || frame.h <= 0 || frame.x < 0 || frame.y < 0) {
                ENGINE_LOG_WARN(Channels::kResource,
                                "'{}' frame '{}' has no area, or a negative corner; it "
                                "is being skipped", sidecar, frame.name);
                continue;
            }
            if (out.FindFrame(frame.name) != nullptr) {
                // NAMES ARE HOW FRAMES ARE ADDRESSED, so two the same means one
                // of them can never be reached. Said out loud rather than
                // silently preferring the first.
                ENGINE_LOG_WARN(Channels::kResource,
                                "'{}' names the frame '{}' twice; only the first is "
                                "used", sidecar, frame.name);
                continue;
            }
            out.frames.push_back(std::move(frame));
        }
    }

    // ---- animations -------------------------------------------------------
    if (HasKey(sheet, "animations") && sheet["animations"].is_array()) {
        for (const Json& entry : sheet["animations"]) {
            if (!entry.is_object()) {
                continue;
            }
            SheetAnimation animation;
            animation.name = ReadString(entry, "name", "", sidecar);
            animation.fps  = ReadFloat(entry, "fps", 12.0f, sidecar);
            animation.loop = ReadBool(entry, "loop", true, sidecar);

            // FALSE WHEN ABSENT, so every sidecar written before this existed
            // keeps behaving exactly as it did. See SheetAnimation::resume.
            animation.resume = ReadBool(entry, "resume", false, sidecar);

            std::string reason;
            if (!IsLegalSubAssetName(animation.name, reason)) {
                ENGINE_LOG_WARN(Channels::kResource,
                                "'{}' has an animation this engine cannot name ({}); it "
                                "is being skipped", sidecar, reason);
                continue;
            }

            // ONE NAMESPACE, DELIBERATELY. A frame and an animation are both
            // addressed as "<sheet>#<name>", so a name used by both would make
            // that path mean two things and whichever was looked up first
            // would win. Refused here, where it is a line in a file somebody
            // can fix, rather than discovered as a sprite showing the wrong
            // picture.
            if (out.FindFrame(animation.name) != nullptr ||
                out.FindAnimation(animation.name) != nullptr) {
                ENGINE_LOG_WARN(Channels::kResource,
                                "'{}' uses the name '{}' for an animation and for "
                                "something else; the animation is being skipped",
                                sidecar, animation.name);
                continue;
            }

            if (HasKey(entry, "frames") && entry["frames"].is_array()) {
                for (const Json& frameName : entry["frames"]) {
                    if (frameName.is_string()) {
                        animation.frames.push_back(frameName.get<std::string>());
                    }
                }
            }
            if (animation.frames.empty()) {
                ENGINE_LOG_WARN(Channels::kResource,
                                "'{}' animation '{}' lists no frames; it is being "
                                "skipped", sidecar, animation.name);
                continue;
            }
            out.animations.push_back(std::move(animation));
        }
    }
}

Json WriteSheet(const SheetSlice& sheet) {
    Json out;
    out["cellWidth"]  = sheet.cellWidth;
    out["cellHeight"] = sheet.cellHeight;
    out["margin"]     = sheet.margin;
    out["spacing"]    = sheet.spacing;

    Json frames = Json::array();
    for (const SheetFrame& frame : sheet.frames) {
        Json entry;
        entry["name"] = frame.name;
        entry["x"]    = frame.x;
        entry["y"]    = frame.y;
        entry["w"]    = frame.w;
        entry["h"]    = frame.h;
        frames.push_back(std::move(entry));
    }
    out["frames"] = std::move(frames);

    Json animations = Json::array();
    for (const SheetAnimation& animation : sheet.animations) {
        Json entry;
        entry["name"]   = animation.name;
        entry["frames"] = animation.frames;
        entry["fps"]    = animation.fps;
        entry["loop"]   = animation.loop;
        entry["resume"] = animation.resume;
        animations.push_back(std::move(entry));
    }
    out["animations"] = std::move(animations);

    return out;
}

// The JSON a set of settings is written as. One function per kind, because the
// two will not stay identical and a shared one would have to be split again.
Json WriteFrom(const TextureImport& settings) {
    Json document;
    document[kFilterKey] = TextureFilterName(settings.filter);

    // Only when there is something to say. An unsliced image writes no
    // "sheet" block at all, so its sidecar looks exactly as it did before
    // spritesheets existed.
    if (settings.sheet != SheetSlice{}) {
        document["sheet"] = WriteSheet(settings.sheet);
    }
    return document;
}

Json WriteFrom(const FontImport& settings) {
    Json document;
    document[kFilterKey] = TextureFilterName(settings.filter);
    return document;
}

void ReadInto(const Json& document, const std::string& sidecar, TextureImport& settings) {
    settings.filter = ReadFilter(document, sidecar, settings.filter);
    ReadSheet(document, sidecar, settings.sheet);
}

void ReadInto(const Json& document, const std::string& sidecar, FontImport& settings) {
    settings.filter = ReadFilter(document, sidecar, settings.filter);
}

// ---------------------------------------------------------------------------
//  Moves every cache entry AT `oldKey`, or UNDER "<oldKey>/", to the matching
//  place under `newKey` - or drops them when `newKey` is empty.
//
//  PREFIX AS WELL AS EXACT, because a FOLDER arrives at FollowAsset too. A
//  folder's own path has no sidecar, so the disk needs nothing done and the
//  header says so - but every asset inside it is cached under "<folder>/...",
//  and an exact-key eviction leaves all of them answering questions about a
//  path that has moved. The Inspector would then show the wrong scaling mode
//  for a file at the new path, and the next save would write that back over a
//  perfectly good sidecar.
//
//  That was the gap between what the header promised about folders and what
//  the cache actually did.
// ---------------------------------------------------------------------------
template <typename Map>
void RecacheUnder(Map& map, const std::string& oldKey, const std::string& newKey) {
    const std::string prefix = oldKey + '/';

    // Collected and re-inserted afterwards rather than in place, because
    // inserting into the map being iterated is how this kind of loop goes
    // wrong even when the container tolerates it.
    std::vector<std::pair<std::string, typename Map::mapped_type>> moved;

    for (auto it = map.begin(); it != map.end();) {
        const bool exact = (it->first == oldKey);
        const bool under = it->first.starts_with(prefix);
        if (!exact && !under) {
            ++it;
            continue;
        }
        if (!newKey.empty()) {
            moved.emplace_back(exact ? newKey
                                     : newKey + it->first.substr(oldKey.size()),
                               it->second);
        }
        it = map.erase(it);
    }

    for (auto& entry : moved) {
        map[entry.first] = std::move(entry.second);
    }
}

// Deletes the sidecar if it is there. True when it is gone afterwards, which
// includes "there was nothing to delete" - see the note on Revert.
bool RemoveSidecar(std::string_view assetPath, std::string& outError) {
    const std::string sidecar = ImportLibrary::SidecarPath(assetPath);

    if (!FileSystem::Exists(sidecar)) {
        outError.clear();
        return true;
    }

    std::size_t removed = 0;
    return FileSystem::Remove(sidecar, removed, outError);
}

// Writes the text of a sidecar out. One place, so the trailing newline and the
// indent cannot differ between the two Save overloads.
bool WriteSidecar(std::string_view assetPath, const Json& document,
                  std::string& outError) {
    // dump(2) and a final newline, matching how a prefab is written in
    // scene/Prefab.cpp - these files are meant to be opened by hand, and one
    // that ends mid-line reads as truncated.
    return FileSystem::WriteTextFile(ImportLibrary::SidecarPath(assetPath),
                                     document.dump(2) + "\n", outError);
}

} // namespace

// ---------------------------------------------------------------------------
//  Names, and reading them back
// ---------------------------------------------------------------------------

const char* TextureFilterName(TextureFilter filter) {
    switch (filter) {
        case TextureFilter::Nearest: return "Nearest";
        case TextureFilter::Linear:  return "Linear";
    }
    return "Nearest";
}

TextureFilter ParseTextureFilter(std::string_view name, bool& outRecognized,
                                 TextureFilter fallback) {
    outRecognized = true;
    if (name == "Nearest") { return TextureFilter::Nearest; }
    if (name == "Linear")  { return TextureFilter::Linear; }
    outRecognized = false;
    return fallback;
}

// ---------------------------------------------------------------------------
//  Sub-asset paths. See the header for the rule and why it is this one.
// ---------------------------------------------------------------------------

bool LooksLikeImage(std::string_view path) {
    // The three formats ResourceManager can decode, plus the spelling of .jpg
    // that cameras and paint programs actually produce. Through HasExtension
    // so that capitals do not decide whether a sheet is a sheet.
    return FileSystem::HasExtension(path, ".png") ||
           FileSystem::HasExtension(path, ".bmp") ||
           FileSystem::HasExtension(path, ".jpg") ||
           FileSystem::HasExtension(path, ".jpeg");
}

bool IsSubAsset(std::string_view path) {
    // THE LAST SEPARATOR, not the first, so that a sheet whose own filename
    // contains one still works: "a.png#b.png#c" is frame "c" of the sheet
    // "a.png#b.png", which is the only reading that lets a sheet be named
    // anything at all.
    const std::size_t hash = path.find_last_of(kSubAssetSeparator);
    if (hash == std::string_view::npos || hash == 0 || hash + 1 >= path.size()) {
        return false;
    }
    // AND THE LEFT-HAND SIDE HAS TO NAME AN IMAGE. This is the whole test:
    // without it "textures/hero#2.png" would be read as frame "2.png" of a
    // sheet called "textures/hero", and an ordinary file that works today
    // would stop loading.
    return LooksLikeImage(path.substr(0, hash));
}

std::string SheetPathOf(std::string_view path) {
    if (!IsSubAsset(path)) {
        return std::string(path);
    }
    return std::string(path.substr(0, path.find_last_of(kSubAssetSeparator)));
}

std::string SubAssetNameOf(std::string_view path) {
    if (!IsSubAsset(path)) {
        return {};
    }
    return std::string(path.substr(path.find_last_of(kSubAssetSeparator) + 1));
}

bool SheetHasAnimation(std::string_view subAssetPath) {
    // A BARE NAME CANNOT BE ANSWERED, not "is not one" - see the header.
    if (!IsSubAsset(subAssetPath)) {
        return false;
    }
    const TextureImport settings =
        ImportLibrary::ForTexture(SheetPathOf(subAssetPath));
    return settings.sheet.FindAnimation(SubAssetNameOf(subAssetPath)) != nullptr;
}

bool ImageExists(std::string_view path) {
    // NOTHING CHOSEN IS NOT BROKEN. Asked first so that neither branch below
    // has to treat "" as a file it could go looking for.
    if (path.empty()) {
        return false;
    }

    if (IsSubAsset(path)) {
        const TextureImport settings = ImportLibrary::ForTexture(SheetPathOf(path));
        if (settings.sheet.FindImage(SubAssetNameOf(path)) != nullptr) {
            return true;
        }
        // AND THEN FALL THROUGH TO THE DISK, which is the loader's own escape
        // hatch rather than a second guess: '#' is a legal filename character,
        // so a file genuinely called "a.png#b" can exist and is read as
        // itself once the sub-asset reading has failed. See the note on the
        // fallback in ResourceManager::LoadTexture.
    }

    return FileSystem::Exists(path);
}

std::string AssetDisplayName(std::string_view path) {
    if (path.empty()) {
        return {};
    }

    // THROUGH THE TWO FUNCTIONS ABOVE rather than looking for a separator
    // here. They are the only things that know the sub-asset format, and a
    // third opinion about where the '#' is would be a third thing to keep in
    // step. SheetPathOf is never empty for a non-empty input, so there is no
    // branch to write.
    const std::string file = SheetPathOf(path);
    const std::string sub  = SubAssetNameOf(path);

    const std::size_t slash = file.find_last_of('/');
    std::string       name =
        (slash == std::string::npos) ? file : file.substr(slash + 1);

    if (!sub.empty()) {
        name += kSubAssetSeparator;
        name += sub;
    }
    return name;
}

bool IsLegalSubAssetName(std::string_view name, std::string& outReason) {
    if (name.empty()) {
        outReason = "a name cannot be empty";
        return false;
    }
    if (name.front() == ' ' || name.back() == ' ') {
        outReason = "a name cannot begin or end with a space";
        return false;
    }

    // ======================================================================
    //  THE REFUSAL THAT MATTERS IS THE DOT, and it is worth being explicit
    //  about why, because it looks over-strict until you follow it through.
    //
    //  This name is CONCATENATED INTO A PATH, and that path is handed to
    //  FileSystem::Resolve, which lexically normalises. A frame named
    //  "../../hero.png" therefore resolves to a real file above the project -
    //  and anything that deletes, renames or writes "the selected asset"
    //  would have found it there. Refusing ".." alone would be enough for
    //  that, but a dot also makes the split rule in the header ambiguous, and
    //  nothing wants one: these are names like "walk_1", not filenames.
    // ======================================================================
    for (const char c : name) {
        if (c == '/' || c == '\\') {
            outReason = "a name cannot contain a slash";
            return false;
        }
        if (c == kSubAssetSeparator) {
            outReason = "a name cannot contain '#', which is what separates it "
                        "from the image it belongs to";
            return false;
        }
        if (c == ':') {
            outReason = "a name cannot contain a colon";
            return false;
        }
        if (c == '.') {
            outReason = "a name cannot contain a dot - these are names like "
                        "\"walk_1\", not file names";
            return false;
        }
    }

    outReason.clear();
    return true;
}

std::string MakeSubAssetPath(std::string_view sheetPath, std::string_view name) {
    std::string reason;
    if (sheetPath.empty() || !IsLegalSubAssetName(name, reason)) {
        return {};
    }
    return std::string(sheetPath) + kSubAssetSeparator + std::string(name);
}

const SheetFrame* SheetSlice::FindFrame(std::string_view name) const {
    for (const SheetFrame& frame : frames) {
        if (frame.name == name) {
            return &frame;
        }
    }
    return nullptr;
}

const SheetAnimation* SheetSlice::FindAnimation(std::string_view name) const {
    for (const SheetAnimation& animation : animations) {
        if (animation.name == name) {
            return &animation;
        }
    }
    return nullptr;
}

const SheetFrame* SheetSlice::FindImage(std::string_view name) const {
    // A FRAME FIRST, because that is what the name usually is and because a
    // frame and an animation may share a name - IsLegalSubAssetName does not
    // forbid it, and the frame is the more specific answer.
    if (const SheetFrame* frame = FindFrame(name)) {
        return frame;
    }

    // AN ANIMATION RESOLVES TO ITS FIRST SURVIVING FRAME. See the note in the
    // header for why that is the useful answer rather than a refusal, and why
    // this rule lives here instead of being written out twice.
    if (const SheetAnimation* animation = FindAnimation(name)) {
        for (const std::string& frameName : animation->frames) {
            if (const SheetFrame* frame = FindFrame(frameName)) {
                return frame;
            }
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
//  Is there anything here a default would not say?
//
//  Compared field by field against a default-constructed one rather than
//  against a written-out list of defaults, so that adding a field cannot leave
//  this function describing the struct it used to be.
// ---------------------------------------------------------------------------

bool IsDefault(const TextureImport& settings) {
    const TextureImport defaults;
    return settings.filter == defaults.filter && settings.sheet == defaults.sheet;
}

bool IsDefault(const FontImport& settings) {
    const FontImport defaults;
    return settings.filter == defaults.filter;
}

// ---------------------------------------------------------------------------
//  The sheet rules, stated once for the writer. See the header.
// ---------------------------------------------------------------------------

bool IsLegalSheet(const SheetSlice& sheet, std::string& outReason) {
    // ---- the grid ---------------------------------------------------------
    //
    // Zero is the legitimate "no grid" state - a sheet whose frames were all
    // placed by hand has one - so only a negative is refused. ReadInt hands
    // back whatever the file says, so a negative really can arrive here.
    if (sheet.cellWidth < 0 || sheet.cellHeight < 0 || sheet.margin < 0 ||
        sheet.spacing < 0) {
        outReason = "the grid numbers cannot be negative";
        return false;
    }

    // ---- the frames -------------------------------------------------------
    for (std::size_t i = 0; i < sheet.frames.size(); ++i) {
        const SheetFrame& frame = sheet.frames[i];

        std::string reason;
        if (!IsLegalSubAssetName(frame.name, reason)) {
            outReason = "frame " + std::to_string(i + 1) + " cannot be named that: " +
                        reason;
            return false;
        }
        // ==================================================================
        //  AND THIS CANNOT CHECK THAT A FRAME IS INSIDE THE IMAGE.
        //
        //  It is handed a SheetSlice and nothing else - no path, no width, no
        //  height - so "x + w is past the right edge" is not a question it is
        //  able to ask. The message said "a corner outside the image" for a
        //  while, which claimed more than the test below performs, and a
        //  reassurance that was never earned is worse than no check at all.
        //
        //  So the rectangle is bounded where the size IS known, in all three
        //  places that know it:
        //    * the Inspector refuses one as it is typed - it has the texture
        //    * CopyFrameOut refuses one at load, and says so in the log
        //    * the Assets panel draws a chip instead of a smear of clamped
        //      edge pixels
        //  A hand-written sidecar can still name one, which is exactly why
        //  those three exist and why this one does not pretend to.
        // ==================================================================
        if (frame.w <= 0 || frame.h <= 0 || frame.x < 0 || frame.y < 0) {
            outReason = "the frame '" + frame.name +
                        "' has no area, or a negative corner";
            return false;
        }

        // AGAINST THE FRAMES BEFORE IT, not through FindFrame, which would
        // find this very frame and report every name as a duplicate.
        for (std::size_t j = 0; j < i; ++j) {
            if (sheet.frames[j].name == frame.name) {
                outReason = "two frames are both called '" + frame.name + "'";
                return false;
            }
        }
    }

    // ---- the animations ---------------------------------------------------
    for (std::size_t i = 0; i < sheet.animations.size(); ++i) {
        const SheetAnimation& animation = sheet.animations[i];

        std::string reason;
        if (!IsLegalSubAssetName(animation.name, reason)) {
            outReason = "animation " + std::to_string(i + 1) +
                        " cannot be named that: " + reason;
            return false;
        }

        // ONE NAMESPACE. "<sheet>#<name>" has to mean one thing, so a frame
        // and an animation cannot share a name - see the note in ReadSheet.
        if (sheet.FindFrame(animation.name) != nullptr) {
            outReason = "'" + animation.name +
                        "' is the name of a frame and of an animation";
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (sheet.animations[j].name == animation.name) {
                outReason = "two animations are both called '" + animation.name + "'";
                return false;
            }
        }

        // AN EMPTY ANIMATION IS DROPPED BY THE READER, which is the single
        // most important line in this function: without it the editor can
        // write one, show it, and lose it on the next reload.
        if (animation.frames.empty()) {
            outReason = "the animation '" + animation.name +
                        "' has no frames in it";
            return false;
        }
    }

    outReason.clear();
    return true;
}

// ---------------------------------------------------------------------------
//  The slicer
// ---------------------------------------------------------------------------

namespace {

// How many whole cells fit along one axis, and what is left over.
//
// Cell n starts at margin + n * (cell + spacing), so the count is the largest
// n + 1 whose cell still ends inside length - margin. Rearranged into the one
// division below, which is the same answer without the loop.
void MeasureAxis(int length, int cell, int margin, int spacing, int& outCount,
                 int& outLeftover) {
    outCount    = 0;
    outLeftover = 0;

    const int usable = length - 2 * margin;
    const int step   = cell + spacing;
    if (usable <= 0 || cell <= 0 || step <= 0) {
        return;
    }

    // Plus one spacing, because the LAST cell is not followed by a gap.
    const int count = (usable + spacing) / step;
    if (count <= 0) {
        return;
    }

    outCount    = count;
    outLeftover = usable - (count * step - spacing);
}

} // namespace

SliceGeometry MeasureGrid(int imageWidth, int imageHeight, const SheetSlice& grid) {
    SliceGeometry out;

    if (imageWidth <= 0 || imageHeight <= 0) {
        out.reason = "the image has no size";
        return out;
    }
    if (grid.cellWidth <= 0 || grid.cellHeight <= 0) {
        out.reason = "a cell needs a width and a height";
        return out;
    }
    if (grid.margin < 0 || grid.spacing < 0) {
        out.reason = "the margin and the spacing cannot be negative";
        return out;
    }

    MeasureAxis(imageWidth, grid.cellWidth, grid.margin, grid.spacing, out.columns,
                out.leftoverX);
    MeasureAxis(imageHeight, grid.cellHeight, grid.margin, grid.spacing, out.rows,
                out.leftoverY);

    if (out.columns <= 0 || out.rows <= 0) {
        // BOTH CLEARED, so that a caller which only looks at `columns` cannot
        // read a row count for a grid that cuts nothing.
        out.columns   = 0;
        out.rows      = 0;
        out.leftoverX = 0;
        out.leftoverY = 0;
        out.reason    = "not one whole cell fits in the image at that size";
        return out;
    }

    out.count = static_cast<long long>(out.columns) * out.rows;
    return out;
}

std::vector<SheetFrame> SliceGrid(int imageWidth, int imageHeight,
                                  const SheetSlice& grid,
                                  std::string_view namePrefix,
                                  std::string& outReason) {
    std::vector<SheetFrame> frames;

    const SliceGeometry geometry = MeasureGrid(imageWidth, imageHeight, grid);
    if (geometry.count <= 0) {
        outReason = geometry.reason;
        return frames;
    }
    if (geometry.count > static_cast<long long>(kMaxSliceFrames)) {
        outReason = "that grid would cut " + std::to_string(geometry.count) +
                    " frames; the most this engine cuts at once is " +
                    std::to_string(kMaxSliceFrames);
        return frames;
    }

    // THE PREFIX IS CHECKED WITH A DIGIT ON THE END, because that is the
    // string which becomes a name - and the digit matters: a prefix of "" is
    // perfectly legal and gives frames called "0", "1", ..., while on its own
    // it would be refused as an empty name.
    std::string reason;
    if (!IsLegalSubAssetName(std::string(namePrefix) + "0", reason)) {
        outReason = "frames cannot be named with that prefix: " + reason;
        return frames;
    }

    // Every x and y below is bounded by the image, because MeasureAxis counted
    // only cells that END inside it. So none of this arithmetic can overflow
    // for any image a renderer will accept.
    frames.reserve(static_cast<std::size_t>(geometry.count));
    for (int row = 0; row < geometry.rows; ++row) {
        for (int column = 0; column < geometry.columns; ++column) {
            SheetFrame frame;
            frame.name = std::string(namePrefix) +
                         std::to_string(row * geometry.columns + column);
            frame.x = grid.margin + column * (grid.cellWidth + grid.spacing);
            frame.y = grid.margin + row * (grid.cellHeight + grid.spacing);
            frame.w = grid.cellWidth;
            frame.h = grid.cellHeight;
            frames.push_back(std::move(frame));
        }
    }

    outReason.clear();
    return frames;
}

bool GuessSheetCell(int imageWidth, int imageHeight, int& outCell) {
    outCell = 0;
    if (imageWidth <= 0 || imageHeight <= 0) {
        return false;
    }

    // The largest cell that divides both ways is their greatest common
    // divisor, and every smaller candidate is one of ITS divisors - so this
    // walks down from there rather than trying a list of fashionable sizes.
    int candidate = std::gcd(imageWidth, imageHeight);

    // A square image's gcd is the whole image: one cell, which is the answer
    // "this is not a grid". Halving it offers the most conservative reading
    // that IS a grid - four quarters - and the person looks at the preview.
    if (candidate == imageWidth && candidate == imageHeight) {
        candidate /= 2;
    }

    // SMALLER THAN THIS IS NOT A GUESS, IT IS NOISE. A width and a height
    // that share no useful factor land here - 101x203 gives 1 - and a
    // thousand one-pixel frames is not what anybody meant.
    constexpr int kSmallestGuess = 4;
    while (candidate >= kSmallestGuess) {
        if (imageWidth % candidate == 0 && imageHeight % candidate == 0 &&
            static_cast<long long>(imageWidth / candidate) *
                    (imageHeight / candidate) >= 2) {
            outCell = candidate;
            return true;
        }
        --candidate;
    }
    return false;
}


// ---------------------------------------------------------------------------
//  Paths
// ---------------------------------------------------------------------------

std::string ImportLibrary::SidecarPath(std::string_view assetPath) {
    // APPENDED to the whole name, extension included. See the header for why
    // not "player.oimport", and for why this is a string operation rather than
    // a std::filesystem::path one.
    //
    // THE STRING IS TAKEN AS GIVEN, with no case folding and no normalising.
    // That is what makes the editor and a shipped game agree: an archive's
    // index is a std::map and matches an entry name byte for byte, so a
    // sidecar found by one spelling of a path must be looked for by the same
    // spelling.
    return std::string(assetPath) + kImportExtension;
}

bool ImportLibrary::IsSidecar(std::string_view path) {
    // Through HasExtension rather than a string compare, so that a file which
    // arrived from a case-insensitive filesystem as ".OIMPORT" is still one.
    // That is not hypothetical; see the note on FileSystem::HasExtension, which
    // exists because a scene saved as .OSCENE was visible to one half of the
    // editor and invisible to the other.
    return FileSystem::HasExtension(path, kImportExtension);
}

bool ImportLibrary::HasSettings(std::string_view assetPath) {
    return FileSystem::Exists(SidecarPath(assetPath));
}

// ---------------------------------------------------------------------------
//  Reading
// ---------------------------------------------------------------------------

TextureImport ImportLibrary::ForTexture(std::string_view assetPath) {
    const std::string key(assetPath);
    if (const auto found = g_textures.find(key); found != g_textures.end()) {
        return found->second;
    }

    TextureImport settings;   // the defaults, which is the answer most of the time

    Json document;
    if (ReadSidecar(assetPath, document)) {
        ReadInto(document, SidecarPath(assetPath), settings);
    }

    g_textures.emplace(key, settings);
    return settings;
}

FontImport ImportLibrary::ForFont(std::string_view assetPath) {
    const std::string key(assetPath);
    if (const auto found = g_fonts.find(key); found != g_fonts.end()) {
        return found->second;
    }

    FontImport settings;

    Json document;
    if (ReadSidecar(assetPath, document)) {
        ReadInto(document, SidecarPath(assetPath), settings);
    }

    g_fonts.emplace(key, settings);
    return settings;
}

// ---------------------------------------------------------------------------
//  Writing
// ---------------------------------------------------------------------------

bool ImportLibrary::Save(std::string_view assetPath, const TextureImport& settings,
                         std::string& outError) {
    // A FRAME HAS NO SETTINGS OF ITS OWN - its sheet's are its. Refused rather
    // than ignored, because '#' is a legal filename character: the write would
    // otherwise SUCCEED, leaving "textures/hero.png#walk_1.oimport" on disk as
    // a permanent orphan that nothing reads and the Assets panel hides.
    if (IsSubAsset(assetPath)) {
        outError = "import settings belong to the whole image, not to one frame of it";
        return false;
    }

    // ---- and nothing the reader would quietly prune -----------------------
    //
    // THE STRICT HALF OF A DELIBERATELY LOPSIDED PAIR. See IsLegalSheet: the
    // reader keeps what it can and names what it drops, which is right for a
    // hand-edited file and intolerable for one this editor wrote a moment ago.
    //
    // It is enforced HERE, at the write, rather than in the Inspector, so that
    // every panel and tool gets the same answer and the rules have exactly one
    // home. An editor that wants to know whether an edit is allowed can make
    // it, offer it, and show this message if it comes back refused.
    std::string sheetReason;
    if (!IsLegalSheet(settings.sheet, sheetReason)) {
        outError = sheetReason;
        return false;
    }
    // THE DEFAULTS ARE WRITTEN AS NO FILE AT ALL. See the header.
    if (IsDefault(settings)) {
        if (!RemoveSidecar(assetPath, outError)) {
            return false;
        }
        g_textures[std::string(assetPath)] = settings;
        Touch();
        ENGINE_LOG_INFO(Channels::kResource,
                        "'{}' is back to the default import settings", assetPath);
        return true;
    }

    if (!WriteSidecar(assetPath, WriteFrom(settings), outError)) {
        return false;
    }

    // THE CACHE IS UPDATED RATHER THAN DROPPED. Dropping it would send the next
    // read to the file, which would also be correct - but this way an editor
    // that has just written a setting cannot possibly read back something else,
    // whatever happens to the disk afterwards.
    g_textures[std::string(assetPath)] = settings;
    Touch();

    if (settings.sheet.IsSliced()) {
        ENGINE_LOG_INFO(Channels::kResource,
                        "'{}' will be scaled {}, in {} frame(s) and {} animation(s)",
                        assetPath, TextureFilterName(settings.filter),
                        settings.sheet.frames.size(),
                        settings.sheet.animations.size());
    } else {
        ENGINE_LOG_INFO(Channels::kResource, "'{}' will be scaled {}", assetPath,
                        TextureFilterName(settings.filter));
    }
    return true;
}

bool ImportLibrary::Save(std::string_view assetPath, const FontImport& settings,
                         std::string& outError) {
    if (IsDefault(settings)) {
        if (!RemoveSidecar(assetPath, outError)) {
            return false;
        }
        g_fonts[std::string(assetPath)] = settings;
        ENGINE_LOG_INFO(Channels::kResource,
                        "'{}' is back to the default import settings", assetPath);
        return true;
    }

    if (!WriteSidecar(assetPath, WriteFrom(settings), outError)) {
        return false;
    }

    g_fonts[std::string(assetPath)] = settings;

    ENGINE_LOG_INFO(Channels::kResource, "'{}' will be scaled {}", assetPath,
                    TextureFilterName(settings.filter));
    return true;
}

bool ImportLibrary::Revert(std::string_view assetPath, std::string& outError) {
    const std::string key(assetPath);

    // Back to the defaults in the cache FIRST, so that a delete which fails
    // still leaves this program agreeing with what it is about to report.
    g_textures.erase(key);
    g_fonts.erase(key);
    Touch();

    const bool had = HasSettings(assetPath);
    if (!RemoveSidecar(assetPath, outError)) {
        return false;
    }

    if (had) {
        ENGINE_LOG_INFO(Channels::kResource,
                        "'{}' is back to the default import settings", assetPath);
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Keeping the sidecar beside its asset
// ---------------------------------------------------------------------------

void ImportLibrary::FollowAsset(std::string_view oldAssetPath,
                                std::string_view newAssetPath) {
    // ======================================================================
    //  THE ALIASED CASES, REFUSED BEFORE ANYTHING IS READ OR DELETED.
    //
    //  This function deletes a file whose path it DERIVED rather than was
    //  given, and that is only safe while the derived path really is a
    //  sidecar. Rename an image to "hero.png.oimport" and it stops being one:
    //  SidecarPath("hero.png") is then the image itself, so the move branch
    //  below would read the image, write a mangled copy of it to
    //  "hero.png.oimport.oimport" and DELETE THE IMAGE.
    //
    //  Silently, too. The move branch logs only on failure, and the copy it
    //  leaves behind is invisible because the Assets panel hides anything that
    //  looks like a sidecar. The copy is not even a copy: it goes through
    //  ReadTextFile, so on Windows a PNG is truncated at the 0x1A byte six
    //  bytes into its own signature. There is no undo for the disk.
    //
    //  All three conditions mean the same thing - there is no sidecar
    //  relationship here to maintain - so there is nothing to do and nothing
    //  is lost by doing it.
    // ======================================================================
    if (oldAssetPath == newAssetPath || IsSidecar(oldAssetPath) ||
        (!newAssetPath.empty() &&
         (IsSidecar(newAssetPath) || SidecarPath(oldAssetPath) == newAssetPath))) {
        return;
    }

    const std::string oldKey(oldAssetPath);
    const std::string newKey(newAssetPath);
    const std::string oldSidecar = SidecarPath(oldAssetPath);

    // What was cached at or under the old path moves with it: the asset it
    // described is not there any more. See RecacheUnder on why "under" is as
    // important as "at".
    RecacheUnder(g_textures, oldKey, newKey);
    RecacheUnder(g_fonts, oldKey, newKey);
    Touch();

    if (!FileSystem::Exists(oldSidecar)) {
        // Nothing to carry. The common case by far, and also what a FOLDER
        // arrives here as - see the header on why that needs no special case
        // and no flag at any of the three call sites.
        //
        // BUT "NO SETTINGS" IS A STATE THAT HAS TO TRAVEL TOO. An orphan
        // sidecar already sitting at the destination would otherwise be picked
        // up by the arriving asset - which is exactly the outcome the
        // overwrite rule in the header exists to prevent, reached from the one
        // direction that rule does not cover.
        if (!newAssetPath.empty() && FileSystem::Exists(SidecarPath(newAssetPath))) {
            std::string error;
            if (RemoveSidecar(newAssetPath, error)) {
                ENGINE_LOG_INFO(Channels::kResource,
                                "removed the import settings left over at '{}', so '{}' "
                                "arrives with the defaults its own file had",
                                SidecarPath(newAssetPath), newAssetPath);
            } else {
                ENGINE_LOG_WARN(Channels::kResource,
                                "'{}' is left over from something else and could not be "
                                "removed ({}); '{}' will be imported with its settings",
                                SidecarPath(newAssetPath), error, newAssetPath);
            }
        }
        return;
    }

    // ---- the asset is gone -------------------------------------------------
    if (newAssetPath.empty()) {
        std::string error;
        if (RemoveSidecar(oldAssetPath, error)) {
            ENGINE_LOG_INFO(Channels::kResource,
                            "removed '{}' along with the asset it described",
                            oldSidecar);
        } else {
            // WORTH SAYING, because what is left behind describes something
            // that no longer exists, and nothing else will ever mention it.
            ENGINE_LOG_WARN(Channels::kResource,
                            "'{}' was deleted but its import settings '{}' could not be "
                            "removed ({}); that file is now an orphan",
                            oldAssetPath, oldSidecar, error);
        }
        return;
    }

    // ---- the asset moved ---------------------------------------------------
    //
    // READ, WRITE, THEN DELETE, rather than FileSystem::Rename or ::Move.
    // Those two deliberately do one job each - Rename changes a name within a
    // folder, Move changes the folder and keeps the name - and this function is
    // asked for the first by the editor's rename dialog and the second by its
    // drag-onto-a-folder, from one signature. Copying a few dozen bytes works
    // for any old-to-new pair, including one that changes both at once.
    //
    // IF THE WRITE FAILS THE ORIGINAL IS STILL THERE, which is the right way
    // round: an orphan sidecar can be found and deleted, whereas settings
    // deleted before the copy succeeded are gone.
    const std::string newSidecar = SidecarPath(newAssetPath);

    std::string text;
    std::string error;
    if (!FileSystem::ReadTextFile(oldSidecar, text, error)) {
        ENGINE_LOG_WARN(Channels::kResource,
                        "could not read '{}' ({}), so '{}' has arrived without its "
                        "import settings",
                        oldSidecar, error, newAssetPath);
        return;
    }
    if (!FileSystem::WriteTextFile(newSidecar, text, error)) {
        ENGINE_LOG_WARN(Channels::kResource,
                        "could not write '{}' ({}), so '{}' has arrived without its "
                        "import settings",
                        newSidecar, error, newAssetPath);
        return;
    }

    std::size_t removed = 0;
    if (!FileSystem::Remove(oldSidecar, removed, error)) {
        ENGINE_LOG_WARN(Channels::kResource,
                        "'{}' was copied to '{}' but the original could not be removed "
                        "({})",
                        oldSidecar, newSidecar, error);
    }

    // The cache was already re-keyed onto the new path by RecacheUnder above,
    // so there is deliberately nothing to do here.
}

// ---------------------------------------------------------------------------
//  Housekeeping
// ---------------------------------------------------------------------------

void ImportLibrary::Clear() {
    g_textures.clear();
    g_fonts.clear();
    Touch();
}

std::uint64_t ImportLibrary::Generation() {
    return g_generation;
}

std::size_t ImportLibrary::CachedCount() {
    return g_textures.size() + g_fonts.size();
}

} // namespace eng
