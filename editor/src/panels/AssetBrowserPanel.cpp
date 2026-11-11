// ============================================================================
//  AssetBrowserPanel.cpp - the Assets panel. See AssetBrowserPanel.h for the
//  two-roots argument.
// ============================================================================

#include "panels/AssetBrowserPanel.h"

#include "AssetDragDrop.h"
#include "EditorApp.h"
#include "ShellOpen.h"
#include "ScriptBuild.h"
#include "ScriptTemplate.h"
#include "panels/HierarchyPanel.h"   // kPayloadEntity, for an entity dropped in here
#include "panels/TilePalettePanel.h"

#include <engine/resource/ImportSettings.h>
#include <engine/scene/Prefab.h>

#include <imgui.h>
#include <imgui_internal.h>   // GetCurrentWindow, for a whole-window drop target

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <cstdio>
#include <cstring>

namespace editor {
namespace {

namespace fs = std::filesystem;

// ONE root: assets/. Scenes, images and scripts all live in it, in whatever
// folder structure suits the game.
//
// THE ASSETS ROOT IS THE EMPTY STRING, and that is not a shortcut. Virtual
// paths are ALREADY relative to assets/ - "scenes/level1.oscene" resolves to
// <project>/assets/scenes/level1.oscene - so the virtual path OF assets/ itself
// is "". Writing "assets" here would ask for <project>/assets/assets.
constexpr const char* kRootAssets = "";

// What to show at the top of the panel. "" is a real virtual path and a
// terrible thing to display.
std::string DisplayPath(const std::string& directory) {
    return directory.empty() ? "assets" : "assets/" + directory;
}

// A tile name from the art it draws: "rock" from "textures/rock.png", and
// "hero_f3" from "textures/hero.png#f3".
//
// THE SHEET'S NAME IS KEPT FOR A FRAME, because a project with four sliced
// sheets has four frames called "f3", and a palette of tiles called f3, f3, f3
// would be unusable. It is a starting point in an editable field rather than a
// decision - but a starting point that is wrong in the common case is worse
// than an empty one.
std::string TileNameFor(const std::string& artPath) {
    const std::string sheet = eng::SheetPathOf(artPath);
    const std::string frame = eng::SubAssetNameOf(artPath);

    const std::size_t slash = sheet.find_last_of('/');
    std::string       stem =
        (slash == std::string::npos) ? sheet : sheet.substr(slash + 1);
    if (const std::size_t dot = stem.find_last_of('.'); dot != std::string::npos) {
        stem.erase(dot);
    }
    return frame.empty() ? stem : stem + "_" + frame;
}

// A folder is not a KIND of asset, so its colour does not belong in
// ColourFor's switch. It is here because two places need it: the tile itself,
// and the little preview that follows the cursor while one is being dragged.
ImVec4 FolderColour() {
    return ImVec4(0.85f, 0.75f, 0.45f, 1.0f);
}

ImVec4 ColourFor(AssetKind kind) {
    switch (kind) {
        case AssetKind::Texture: return ImVec4(0.55f, 0.78f, 0.95f, 1.0f);
        case AssetKind::Scene:   return ImVec4(0.95f, 0.80f, 0.45f, 1.0f);
        case AssetKind::Script:  return ImVec4(0.65f, 0.90f, 0.65f, 1.0f);
        case AssetKind::Font:    return ImVec4(0.88f, 0.70f, 0.95f, 1.0f);
        case AssetKind::Tile:    return ImVec4(0.55f, 0.92f, 0.85f, 1.0f);
        case AssetKind::Prefab:  return ImVec4(0.55f, 0.78f, 0.95f, 1.0f);
        case AssetKind::Unknown: break;
    }
    return ImVec4(0.62f, 0.62f, 0.66f, 1.0f);
}

const char* LabelFor(AssetKind kind) {
    switch (kind) {
        case AssetKind::Texture: return "IMG";
        case AssetKind::Scene:   return "SCN";
        case AssetKind::Script:  return "CPP";
        case AssetKind::Font:    return "TTF";
        case AssetKind::Tile:    return "TILE";
        case AssetKind::Prefab:  return "PFB";
        case AssetKind::Unknown: break;
    }
    return "---";
}

// Joined carefully rather than concatenated, because the assets root IS the
// empty string and "" + "/" + name would start with a slash - which means
// something quite different.
std::string JoinPath(const std::string& directory, const std::string& leaf) {
    return directory.empty() ? leaf : directory + "/" + leaf;
}

std::string ParentOf(const std::string& directory) {
    const std::size_t slash = directory.find_last_of('/');
    return (slash == std::string::npos) ? std::string() : directory.substr(0, slash);
}

std::string LeafOf(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

// ---------------------------------------------------------------------------
//  COULD THIS MOVE ACTUALLY HAPPEN? Asked before the target lights up.
//
//  FileSystem::Move is the authority and refuses all of this for real, so
//  nothing here is load-bearing. It exists because a target that highlights
//  invitingly and then declines on release looks exactly like a broken
//  feature - worse than one that never lit up at all, because the user now
//  believes they did it wrong. Same argument as HierarchyPanel's
//  SelectionReparentIsLegal.
//
//  The Exists() call is affordable because ImGui only reaches a drop target
//  for the ONE item the cursor is actually over. This is not run per tile.
// ---------------------------------------------------------------------------
bool MoveLooksLegal(const std::string& from, const std::string& toDirectory) {
    if (from.empty()) {
        return false;   // the assets root itself
    }
    if (eng::IsSubAsset(from)) {
        // A SLICED FRAME IS NOT A FILE, so there is nowhere to move it to. It
        // matters that this is refused HERE and not only by FileSystem::Move,
        // because this function is what decides whether a folder tile and
        // every step of the path trail LIGHT UP as a drop target - and a target
        // that highlights and then refuses is worse than one that never
        // offered. See the note above.
        return false;
    }
    if (eng::IsSubAsset(from)) {
        // A SLICED FRAME IS NOT A FILE, so there is nowhere to move it to. It
        // matters that this is refused HERE and not only by FileSystem::Move,
        // because this function is what decides whether a folder tile and
        // every step of the path trail LIGHT UP as a drop target - and a target
        // that highlights and then refuses is worse than one that never
        // offered. See the note above.
        return false;
    }
    if (eng::IsSubAsset(from)) {
        // A SLICED FRAME IS NOT A FILE, so there is nowhere to move it to. It
        // matters that this is refused HERE and not only by FileSystem::Move,
        // because this function is what decides whether a folder tile and
        // every step of the path trail LIGHT UP as a drop target - and a target
        // that highlights and then refuses is worse than one that never
        // offered. See the note above.
        return false;
    }
    if (eng::IsSubAsset(from)) {
        // A SLICED FRAME IS NOT A FILE, so there is nowhere to move it to. It
        // matters that this is refused HERE and not only by FileSystem::Move,
        // because this function is what decides whether a folder tile and
        // every step of the path trail LIGHT UP as a drop target - and a target
        // that highlights and then refuses is worse than one that never
        // offered. See the note above.
        return false;
    }
    if (eng::IsSubAsset(from)) {
        // A SLICED FRAME IS NOT A FILE, so there is nowhere to move it to. It
        // matters that this is refused HERE and not only by FileSystem::Move,
        // because this function is what decides whether a folder tile and
        // every step of the path trail LIGHT UP as a drop target - and a target
        // that highlights and then refuses is worse than one that never
        // offered. See the note above.
        return false;
    }
    if (eng::IsSubAsset(from)) {
        // A SLICED FRAME IS NOT A FILE, so there is nowhere to move it to. It
        // matters that this is refused HERE and not only by FileSystem::Move,
        // because this function is what decides whether a folder tile and
        // every step of the path trail LIGHT UP as a drop target - and a target
        // that highlights and then refuses is worse than one that never
        // offered. See the note above.
        return false;
    }
    if (eng::IsSubAsset(from)) {
        // A SLICED FRAME IS NOT A FILE, so there is nowhere to move it to. It
        // matters that this is refused HERE and not only by FileSystem::Move,
        // because this function is what decides whether a folder tile and
        // every step of the path trail LIGHT UP as a drop target - and a target
        // that highlights and then refuses is worse than one that never
        // offered. See the note above.
        return false;
    }
    // ---- a sliced frame has nowhere to go --------------------------------
    //
    // Refused HERE and not only by FileSystem::Move, because this function is
    // what decides whether a folder tile and every step of the path trail
    // LIGHT UP as a drop target - and a target that highlights and then
    // refuses is worse than one that never offered. See the note above.
    //
    // ======================================================================
    //  AND THE DISK IS ASKED, BECAUSE THE NAME IS NOT ENOUGH.
    //
    //  IsSubAsset is purely lexical: the last '#' whose left-hand side ends in
    //  an image extension. So it says yes to a REAL FILE called
    //  "sheet.png#backup", and to a real FOLDER called "art.png#old" - which
    //  this panel's own "+ Folder" will happily create, since its name check
    //  refuses the characters a filesystem refuses and '#' is not one of them.
    //
    //  Refusing on the name alone made both of those unmovable by any gesture,
    //  for ever, while the drag preview went on offering the move and no
    //  target ever lit up. Nothing was said, because a target that never
    //  opens has nowhere to say it.
    //
    //  The same ambiguity ResourceManager::LoadTexture resolves the same way:
    //  if something really is there under that name, it is a file and it moves.
    // ======================================================================
    if (eng::IsSubAsset(from) && !eng::FileSystem::Exists(from)) {
        return false;
    }

    // A folder cannot go inside itself or inside its own children. The
    // separator test is what makes this exact: "art" is a prefix of both
    // "art/ui" and "artwork", and only the first is inside it.
    if (toDirectory == from ||
        (toDirectory.size() > from.size() && toDirectory.starts_with(from) &&
         toDirectory[from.size()] == '/')) {
        return false;
    }

    if (ParentOf(from) == toDirectory) {
        return false;   // already in there; the drag would achieve nothing
    }

    // Moving onto a name that is taken would mean overwriting it, which is
    // never what dragging something somewhere is asking for.
    return !eng::FileSystem::Exists(JoinPath(toDirectory, LeafOf(from)));
}

// ---------------------------------------------------------------------------
//  The wrapping grid, as one object that owns the arithmetic.
//
//  ImGui has no flow layout, so a grid is built by hand: work out how many
//  tiles fit across, and call SameLine() between them. That was four lines
//  inline for as long as every row was the same kind of thing. A sliced
//  spritesheet shows its frames underneath it, indented, which needs the row
//  BROKEN before and after the run - and three rules that are each easy to get
//  wrong on their own have to hold together:
//
//  SameLine COMES BEFORE THE TILE, not after it. The old code called it after,
//  guarded by "unless this is the last one" to avoid a trailing SameLine with
//  nothing following. Putting it first means it always precedes a real tile and
//  that guard disappears - which matters, because with a run of parts in the
//  middle there is no longer one simple answer to "is this the last one".
//
//  NOT CALLING SameLine IS THE ROW BREAK. There is no NewLine() anywhere here:
//  NewLine after a SameLine that was already issued leaves a blank row, and
//  after one that was not it leaves two.
//
//  `column` IS A RUNNING TOTAL AND IS RESET AT EVERY BREAK. A break that
//  forgot to reset it would throw every later row boundary off by
//  columns - (column % columns), and the symptom is a grid that looks
//  almost right.
//
//  INDENT AND UNINDENT ONLY EVER RUN ON A BROKEN LINE, which is why SetIndent
//  breaks first and is the only thing in this file that touches ImGui::Indent.
//  An Unindent issued with a SameLine still pending draws the next tile on top
//  of the leftmost part of the run, and ImGui's hover test and this panel's own
//  FolderUnderPoint then give different answers for the same pixel.
//
//  MEASURED INSIDE THE GRID'S CHILD WINDOW. The old code measured in the
//  Assets window before BeginChild, which is optimistic by the width of the
//  scrollbar the child grows once it has more tiles than fit - so the last
//  column could be pushed past the right edge, where ItemAdd clips it: a
//  folder tile that is drawn, cannot be hovered, records no position and
//  therefore silently stops accepting files dropped from Explorer.
// ---------------------------------------------------------------------------
class FlowGrid {
public:
    explicit FlowGrid(float iconSize)
        : m_cellWidth(iconSize + ImGui::GetStyle().ItemSpacing.x) {
        Measure();
    }

    // Call before every tile. Opens a new row when this one is full.
    void Next() {
        if (m_lineOpen && m_column < m_columns) {
            ImGui::SameLine();
        } else {
            m_column = 0;
        }
        ++m_column;
        m_lineOpen = true;
    }

    void Break() {
        m_column   = 0;
        m_lineOpen = false;
    }

    // Breaks the line first, always. See the note above.
    void SetIndent(float indent) {
        if (indent == m_indent) {
            return;
        }
        Break();
        if (m_indent > 0.0f) {
            ImGui::Unindent(m_indent);
        }
        m_indent = indent;
        if (m_indent > 0.0f) {
            ImGui::Indent(m_indent);
        }
        Measure();
    }

    float Indent() const { return m_indent; }

private:
    void Measure() {
        const float available = ImGui::GetContentRegionAvail().x;
        m_columns = static_cast<int>(available / m_cellWidth);
        if (m_columns < 1) {
            m_columns = 1;   // one per row rather than none at all
        }
    }

    float m_cellWidth = 0.0f;
    int   m_column    = 0;
    int   m_columns   = 1;
    float m_indent    = 0.0f;
    bool  m_lineOpen  = false;
};

} // namespace

AssetBrowserPanel::AssetBrowserPanel() {
    Refresh();
}

void AssetBrowserPanel::Navigate(const std::string& virtualDirectory) {
    m_directory = virtualDirectory;
    Refresh();
}

void AssetBrowserPanel::Refresh() {
    // ======================================================================
    //  BUILT INTO A LOCAL AND SWAPPED IN AT THE END, which is not tidiness.
    //
    //  The old rows are still holding their images while the new ones are
    //  asked for, so every image that is still in the folder is answered from
    //  ResourceManager's cache. Clearing first - which is what this function
    //  used to do, on its very first line - let go of this panel's share of
    //  every image BEFORE asking for any of them, so anything no sprite in the
    //  open scene happened to be using was read off the disk, decoded and
    //  uploaded again on every single Refresh. Including the one the Build
    //  Scripts button causes.
    //
    //  That cost was invisible while a refresh only happened when somebody
    //  navigated. Expanding a spritesheet is a refresh too.
    // ======================================================================
    std::vector<eng::FileSystem::DirEntry> entries;

    m_valid = eng::FileSystem::ListDirectory(m_directory, entries);
    if (!m_valid) {
        entries.clear();
    }

    // ---- the .oimport files are not shown ---------------------------------
    //
    // One per image that has settings, sitting beside it, holding one line.
    // Showing them would nearly double the length of a sprite folder with
    // files nobody needs to open: they are edited through the Inspector, which
    // is reached by selecting the IMAGE.
    //
    // HIDDEN, NOT IGNORED. They are still shipped by the exporter, and
    // renaming, moving or deleting an image still takes its settings with it -
    // see eng::ImportLibrary::FollowAsset - so there is nothing you could want
    // to do to a sidecar that this panel has now made impossible.
    //
    // BEFORE THE TILES ARE BUILT, so that a sidecar never becomes a row. It
    // used to matter for a second reason that no longer exists: there was a
    // separate vector of images indexed in step with this list, and filtering
    // one without the other slid every image one place along from the file it
    // belonged to. A row carries its own picture now - see Tile.
    std::erase_if(entries, [](const eng::FileSystem::DirEntry& entry) {
        return !entry.isDirectory && eng::ImportLibrary::IsSidecar(entry.virtualPath);
    });

    std::vector<Tile> tiles;
    tiles.reserve(entries.size());
    for (const eng::FileSystem::DirEntry& entry : entries) {
        Tile tile;
        tile.path  = entry.virtualPath;
        tile.label = entry.name;
        tile.file  = entry;
        tile.kind  = entry.isDirectory ? AssetKind::Unknown
                                       : ClassifyAsset(entry.virtualPath);

        if (!entry.isDirectory && tile.kind == AssetKind::Texture) {
            tile.texture = eng::ResourceManager::LoadTexture(entry.virtualPath);
            AddSheetParts(tile);
        }
        tiles.push_back(std::move(tile));
    }

    m_tiles.swap(tiles);

    // AND THE OLD ROWS DO NOT DIE HERE. Refresh can be called from inside
    // Draw, with this frame's draw list already holding their textures' SDL
    // handles. See m_retired.
    //
    // APPENDED RATHER THAN ASSIGNED, because two refreshes can happen in one
    // frame - a delete confirms, which refreshes, and the pending-expand from
    // the same frame refreshes again - and the first batch must not be
    // dropped by the second.
    m_retired.insert(m_retired.end(), std::make_move_iterator(tiles.begin()),
                     std::make_move_iterator(tiles.end()));
}

// ---------------------------------------------------------------------------
//  Does this image open, and if it is open, what is inside it?
//
//  CALLED ONCE PER IMAGE PER REFRESH. Never per frame and never per part - the
//  import settings come back BY VALUE with two vectors inside them, which the
//  engine's own header calls a deliberate trade priced for one caller asking
//  once a frame. Multiplying that by every image in the folder, in a panel
//  whose header refuses per-folder work per frame, is how that trade stops
//  being worth it.
//
//  AND NEVER WITH A '#' PATH, which is the early return below. ImportLibrary
//  caches on whatever string it is handed, and its rename bookkeeping matches
//  an exact key or the prefix "<old>/" - so an entry under
//  "textures/hero.png#f0" would sit there as a stale set of defaults for the
//  rest of the session, surviving a rename of the sheet it came from. A real
//  file whose own name contains a hash can reach this, which is the only
//  reason the test is here at all.
// ---------------------------------------------------------------------------
void AssetBrowserPanel::AddSheetParts(Tile& sheet) {
    if (eng::IsSubAsset(sheet.path)) {
        return;
    }

    const eng::TextureImport settings = eng::ImportLibrary::ForTexture(sheet.path);

    // ---- a selection whose part has been sliced away ----------------------
    //
    // ABOVE THE "is it still sliced?" RETURN, and that placement is the whole
    // point. It used to sit below it, so removing ONE frame did the friendly
    // thing while removing them ALL - "Remove every frame", or unticking the
    // spritesheet box - left the selection naming a part of a sheet that is no
    // longer a sheet: no tile anywhere carried that path, so no border was
    // drawn, nothing was said, and Delete advised fixing it in a Frames list
    // that was now empty. The two gestures behaved differently for no reason
    // anybody could guess. An empty slice answers null from both Find calls,
    // so the same code handles both once it is allowed to run.
    //
    // DROPPED HERE AND NOT IN PruneSelection, which tests whether the SHEET
    // still exists - deliberately, because that is what makes a frame
    // selectable at all. This is the other half: the file is still there and
    // the name inside it is not, which only the slice can answer.
    if (m_pickedIsPart && eng::SheetPathOf(m_picked) == sheet.path) {
        const std::string name = eng::SubAssetNameOf(m_picked);
        if (settings.sheet.FindFrame(name) == nullptr &&
            settings.sheet.FindAnimation(name) == nullptr) {
            // THROUGH Pick, not by hand. Clearing selectedAsset while setting
            // m_picked to the sheet left the border on the sheet's tile and the
            // Inspector showing nothing - the two disagreeing about one
            // selection, which is exactly what the note on m_picked says Pick
            // exists to prevent. The status line claimed the sheet was
            // selected and the panel beside it said otherwise.
            Pick(sheet.path, false);
            m_statusIsError = false;
            std::snprintf(m_status, sizeof(m_status),
                          "'%s' is no longer a part of %s, so the sheet is selected "
                          "instead", name.c_str(), sheet.path.c_str());
        }
    }

    if (!settings.sheet.IsSliced()) {
        return;
    }

    sheet.expandable      = true;
    sheet.expanded        = IsExpanded(sheet.path);
    sheet.sheetFrames     = settings.sheet.frames.size();
    sheet.sheetAnimations = settings.sheet.animations.size();

    if (!sheet.expanded) {
        return;
    }

    // ---- what is wrong with the sheet itself, asked once ------------------
    const bool usable = sheet.texture && sheet.texture->native != nullptr &&
                        sheet.texture->width > 0 && sheet.texture->height > 0 &&
                        !sheet.texture->isPlaceholder;
    const std::string sheetTrouble =
        usable ? std::string() : std::string("the sheet itself could not be loaded");

    const int sheetWidth  = usable ? sheet.texture->width : 0;
    const int sheetHeight = usable ? sheet.texture->height : 0;

    sheet.parts.reserve(settings.sheet.frames.size() +
                        settings.sheet.animations.size());

    const auto addPart = [&](const std::string& name, const eng::SheetFrame* rect,
                             const eng::SheetAnimation* animation) {
        Tile part;
        part.path = eng::MakeSubAssetPath(sheet.path, name);
        if (part.path.empty()) {
            // A NAME THIS ENGINE CANNOT ADDRESS. The reader skips one too, so
            // this is reachable only from a file written before that rule
            // existed - said out loud rather than drawn as a tile that nothing
            // could be dropped from.
            m_statusIsError = true;
            std::snprintf(m_status, sizeof(m_status),
                          "%s has a part called '%s', which is not a name this "
                          "engine can use", sheet.path.c_str(), name.c_str());
            ENGINE_LOG_ERROR(eng::Channels::kEditor,
                             "'{}' has a part named '{}' that cannot be addressed",
                             sheet.path, name);
            return;
        }

        part.label     = name;
        part.kind      = AssetKind::Texture;
        part.sheetPath = sheet.path;

        // THE SHEET'S OWN TEXTURE, shared. This is what makes opening a
        // 96-frame sheet cost no textures at all: every part draws a
        // rectangle of one image that was already loaded for the thumbnail.
        part.texture = sheet.texture;

        if (animation != nullptr) {
            part.isAnimation = true;
            part.fps         = animation->fps;
            part.loop        = animation->loop;
            part.frameCount  = animation->frames.size();
            if (rect != nullptr) {
                part.firstFrame = rect->name;
            }
        }

        if (!sheetTrouble.empty()) {
            part.trouble = sheetTrouble;
        } else if (rect == nullptr) {
            part.trouble = "none of this animation's frames are in the sheet any more";
        } else if (rect->w <= 0 || rect->h <= 0 || rect->x < 0 || rect->y < 0 ||
                   rect->w > sheetWidth || rect->h > sheetHeight ||
                   rect->x > sheetWidth - rect->w ||
                   rect->y > sheetHeight - rect->h) {
            // ==============================================================
            //  THE SAME TEST CopyFrameOut MAKES, IN THE SAME SUBTRACTION FORM,
            //  and it is load-bearing rather than defensive.
            //
            //  ImportLibrary::Save cannot refuse this: IsLegalSheet is handed a
            //  SheetSlice with no width or height in it, so it can only reject
            //  a non-positive size or a negative corner. The Inspector refuses
            //  an out-of-range rectangle as it is typed, because it has the
            //  texture - but a hand-written sidecar still can.
            //
            //  SUBTRACTION, NOT x + w > width, which is signed overflow for a
            //  large enough number and which a compiler is entitled to delete.
            //
            //  Without this, uv coordinates past 1 sample CLAMPED: a smear of
            //  repeated edge pixels that looks like a real frame, while the
            //  same part dragged into the scene fails loudly in the log. The
            //  panel must not look fine about something the engine refuses.
            // ==============================================================
            part.trouble = "the frame falls outside the image";
        } else {
            const float wide = static_cast<float>(sheetWidth);
            const float high = static_cast<float>(sheetHeight);
            part.u0 = static_cast<float>(rect->x) / wide;
            part.v0 = static_cast<float>(rect->y) / high;
            part.u1 = static_cast<float>(rect->x + rect->w) / wide;
            part.v1 = static_cast<float>(rect->y + rect->h) / high;
            part.pixelW = rect->w;
            part.pixelH = rect->h;
        }

        sheet.parts.push_back(std::move(part));
    };

    for (const eng::SheetFrame& frame : settings.sheet.frames) {
        addPart(frame.name, &frame, nullptr);
    }

    // ---- then the animations ----------------------------------------------
    //
    // AFTER the frames, because that is the order the Inspector lists them in
    // and a grid that disagreed with the panel beside it about the order would
    // make counting them pointless.
    for (const eng::SheetAnimation& animation : settings.sheet.animations) {
        // THE FIRST FRAME THAT STILL EXISTS, which is the same walk
        // CreateSubAssetTexture makes - not the first one LISTED. A frame
        // renamed in the Inspector leaves its old name behind in every
        // animation that used it, and resolving one skips a name that matches
        // nothing. The picture on this tile has to be the picture a drop
        // actually produces.
        const eng::SheetFrame* first = nullptr;
        for (const std::string& member : animation.frames) {
            if (const eng::SheetFrame* found = settings.sheet.FindFrame(member);
                found != nullptr) {
                first = found;
                break;
            }
        }
        addPart(animation.name, first, &animation);
    }
}

bool AssetBrowserPanel::IsExpanded(const std::string& sheetPath) const {
    return std::find(m_expandedSheets.begin(), m_expandedSheets.end(), sheetPath) !=
           m_expandedSheets.end();
}

void AssetBrowserPanel::DrawBreadcrumb() {
    // "Up" is greyed out AT the root rather than hidden, so the toolbar keeps
    // its shape as you navigate and the buttons stay where your hand expects.
    //
    // Being "at the root" is a comparison against the root itself, not a test
    // for an empty parent - because the assets root IS the empty string, and
    // an empty-parent test would grey Up out inside every first-level folder.
    ImGui::BeginDisabled(m_directory == kRootAssets);
    if (ImGui::SmallButton("Up")) {
        Navigate(ParentOf(m_directory));
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) {
        Refresh();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Reads the folder again.\n"
                          "An image edited outside the editor is picked up when\n"
                          "you come back to the window - see\n"
                          "ResourceManager::ReloadChangedTextures.");
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("+ Script")) {
        m_openNewScript = true;
        std::snprintf(m_newScriptName, sizeof(m_newScriptName), "%s", "NewScript");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Tile")) {
        m_openNewTile = true;

        // Prefilled from whatever is selected, because the usual way to reach
        // this button is "I have just imported a sheet and I want a tile for
        // it". Having to retype the path you are looking at is the kind of
        // small friction that makes a tool feel like paperwork.
        m_newTileArt[0]  = '\0';
        m_newTileName[0] = '\0';
        m_newTileAsImage = false;

        // ==================================================================
        //  THE FRAME ITSELF WHEN A FRAME IS PICKED - which is Stage 5, and
        //  this is the line the note that used to be here said would change.
        //
        //  It used to resolve a picked frame back to its sheet, because a
        //  .otile could only be a five-band sheet and one frame of a
        //  spritesheet is not one. Now it can be an image, and one frame is
        //  precisely what an image tile wants, so the frame is kept.
        //
        //  AND THE KIND IS DECIDED BY ASKING THE LOADER, not by guessing from
        //  the path. A whole image might be a real five-band sheet - try it as
        //  one, and fall back to an image when it is not the right shape. That
        //  is the same principle as the dialog's live preview: the only rule
        //  about what a .otile accepts lives in TileAsset::Deserialize, and
        //  anything here that repeated it would drift from it.
        //
        //  Still a READ of m_picked and not an action on it, which is the rule
        //  stated on m_picked in the header.
        // ==================================================================
        if (ClassifyAsset(eng::SheetPathOf(m_picked)) == AssetKind::Texture) {
            std::snprintf(m_newTileArt, sizeof(m_newTileArt), "%s", m_picked.c_str());
            std::snprintf(m_newTileName, sizeof(m_newTileName), "%s",
                          TileNameFor(m_picked).c_str());

            if (eng::IsSubAsset(m_picked)) {
                m_newTileAsImage = true;   // a frame is one picture, never five bands
            } else {
                eng::Json      probeNode = eng::Json::object();
                probeNode["sheet"]       = m_picked;
                eng::TileAsset probe;
                std::string    ignored;
                m_newTileAsImage = !probe.Deserialize(probeNode, ignored);
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Make a .otile from a five-band tile sheet.\n"
                          "Select the sheet first and its path is filled in.");
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("+ Folder")) {
        m_openNewFolder    = true;
        m_newFolderName[0] = '\0';
    }

    // Building scripts by hand. It normally happens on its own when the editor
    // regains focus, but having a button matters: it makes the step visible
    // rather than magic, and it gives you a way to retry after fixing a
    // compile error without alt-tabbing out and back.
    ImGui::SameLine();
    ImGui::BeginDisabled(!ScriptBuild::HasCompiler());
    if (ImGui::SmallButton("Build Scripts")) {
        // Emptied BEFORE the build is started - see ClearConsoleForCompile in
        // EditorApp.h. Guarded on IsBuilding so that a second click, which
        // BeginAsync then refuses because one is already running, cannot wipe
        // the output of the build that is still going.
        if (!ScriptBuild::IsBuilding()) {
            ClearConsoleForCompile();
        }

        // Started, not waited for - the editor keeps drawing while the
        // compiler runs. EditorApp collects the result once it is ready and
        // puts the summary in the menu bar. See ScriptBuild.h.
        if (ScriptBuild::BeginAsync()) {
            std::snprintf(m_status, sizeof(m_status), "building scripts...");
        } else if (ScriptBuild::IsBuilding()) {
            std::snprintf(m_status, sizeof(m_status), "already building");
        }
        Refresh();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", ScriptBuild::HasCompiler()
                                    ? ScriptBuild::CompilerDescription().c_str()
                                    : "no C++ compiler was found on this machine");
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::SliderFloat("##icon", &m_iconSize, 40.0f, 128.0f, "icon %.0f");

    DrawPathTrail();
}

void AssetBrowserPanel::DrawPathTrail() {
    // ========================================================================
    //  THE PATH, AS BUTTONS. This line used to be plain grey text saying
    //  where you were. It now says the same thing and does two more jobs:
    //  every step jumps straight to that folder, and every step ACCEPTS A
    //  DROP.
    //
    //  The second one is what makes moving something back OUT of a folder
    //  possible at all. Without it the only route out is to navigate up
    //  first - and by then the thing you wanted to move is no longer on
    //  screen to pick up. A one-way reorganiser is not a reorganiser.
    // ========================================================================
    if (ImGui::SmallButton("assets")) {
        Navigate(kRootAssets);
    }
    AcceptMoveInto(kRootAssets);

    std::string walked;
    std::size_t start = 0;
    while (start < m_directory.size()) {
        const std::size_t slash = m_directory.find('/', start);
        const std::string part  = (slash == std::string::npos)
                                      ? m_directory.substr(start)
                                      : m_directory.substr(start, slash - start);
        walked = walked.empty() ? part : walked + "/" + part;

        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextDisabled("/");
        ImGui::SameLine(0.0f, 4.0f);

        // PushID by depth, because a label is an identity in ImGui and a
        // project with ui/ inside enemies/ui/ would otherwise have two
        // buttons that are the same button.
        ImGui::PushID(static_cast<int>(start));
        if (ImGui::SmallButton(part.c_str())) {
            Navigate(walked);
        }
        AcceptMoveInto(walked);
        ImGui::PopID();

        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
}

void AssetBrowserPanel::DrawEntry(const Tile& tile, int index) {
    // Worked out once, in Refresh, rather than per frame per tile. A folder
    // has no kind, which is why Tile::kind is Unknown for one.
    const AssetKind kind = tile.kind;

    // PushID(index) makes every widget in this tile unique, so twenty tiles
    // drawn by the same code are twenty separate things as far as ImGui is
    // concerned. BeginGroup makes the icon and its label behave as one item
    // for layout purposes.
    ImGui::PushID(index);
    ImGui::BeginGroup();

    const ImVec2 iconSize(m_iconSize, m_iconSize);

    // The thumbnail, or a coloured tile with a three-letter type on it. A tile
    // rather than blank space, so every entry is the same size and the grid
    // does not go ragged.
    //  THE ROW'S OWN PICTURE, with no index into anything. There used to be a
    //  second vector here, indexed in step with the entries, and a guard
    //  against reading past its end that would have drawn the wrong image
    //  rather than crashed. See the note on Tile in the header.
    bool drewImage = false;
    if (tile.texture && tile.texture->native != nullptr) {
        ImGui::Image(reinterpret_cast<ImTextureID>(tile.texture->native), iconSize);
        drewImage = true;
    }
    if (!drewImage) {
        const ImVec4 colour = tile.IsDirectory() ? FolderColour() : ColourFor(kind);
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImVec4(colour.x * 0.35f, colour.y * 0.35f,
                                     colour.z * 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, colour);
        ImGui::Button(tile.IsDirectory() ? "DIR" : LabelFor(kind), iconSize);
        ImGui::PopStyleColor(2);
    }

    const bool iconHovered = ImGui::IsItemHovered();
    const bool iconClicked = iconHovered && ImGui::IsMouseDoubleClicked(0);

    // ---- this tile is a DRAG SOURCE --------------------------------------
    //
    // EVERYTHING IS DRAGGABLE, folders included. It used to be only the kinds
    // that meant something to an entity, which was the right rule while a
    // drag could only ever end ON an entity. A drag can now also end on a
    // folder, and something that cannot be picked up cannot be reorganised -
    // which would have left folders, .json files and .txt files as the only
    // things in the project that could not be tidied away.
    const char* payloadId = tile.IsDirectory() ? kPayloadAssetItem : PayloadIdFor(kind);
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        // The virtual path is what gets carried, including its terminating
        // zero so the receiving end can use it directly as a C string. ImGui
        // takes a COPY of it, which matters: passing a pointer into m_entries
        // would dangle the moment a refresh happened mid-drag.
        ImGui::SetDragDropPayload(payloadId, tile.path.c_str(),
                                  tile.path.size() + 1);

        // What the cursor carries while dragging. Worth the few lines: a drag
        // with no feedback feels broken even when it is working perfectly.
        ImGui::TextColored(tile.IsDirectory() ? FolderColour() : ColourFor(kind), "%s",
                           tile.IsDirectory() ? "DIR" : LabelFor(kind));
        ImGui::SameLine();
        ImGui::TextUnformatted(tile.label.c_str());
        if (!tile.IsDirectory() && kind == AssetKind::Script) {
            ImGui::TextDisabled("drop on an entity to attach it");
        } else if (!tile.IsDirectory() && kind == AssetKind::Texture) {
            ImGui::TextDisabled("drop in the Scene view to place it");
        }
        // ONLY FOR SOMETHING THAT CAN BE MOVED. MoveLooksLegal refuses every
        // sub-asset and is consulted before any folder or breadcrumb lights
        // up, so offering this for a frame would be a promise followed by
        // silence - the inverse of what this editor does everywhere else.
        if (tile.IsFile()) {
            ImGui::TextDisabled("drop on a folder, or on the path above, to "
                                "move it");
        }
        ImGui::EndDragDropSource();
    }

    // ======================================================================
    //  CAPTURED BEFORE THE ARROW, and that is the whole reason it is a local.
    //
    //  PushTextWrapPos reads the LIVE cursor, so taking it after an arrow plus
    //  a SameLine would set the wrap position a button's width past the tile's
    //  right edge - and a long filename would then push the grid apart, which
    //  is exactly what this wrap exists to prevent.
    // ======================================================================
    const float labelLeft = ImGui::GetCursorPos().x;

    // ---- the lid, for a sliced spritesheet --------------------------------
    //
    // IN THE LABEL ROW RATHER THAN OVER THE ICON, which is not a cosmetic
    // choice. Everything in this function that binds to "the last item" comes
    // after EndGroup and binds to the GROUP, so an item inside the group
    // cannot steal any of it - but only if it is submitted after the things
    // that read the icon directly. `iconHovered` was taken from the icon
    // above, and the drag source was opened on it, so a press that begins on
    // this arrow can neither select the sheet nor start dragging it.
    //
    // ZERO FramePadding makes the button exactly one line of text tall -
    // GetFrameHeight is FontSize + FramePadding.y * 2 - so the arrow itself
    // adds no height.
    //
    // IT DOES TAKE WIDTH FROM THE LABEL, THOUGH, and a long filename beside
    // it therefore wraps onto one more line than the same name on a tile with
    // no arrow - so a row holding a sliced sheet can be a line taller. Left
    // as it is on purpose: the alternatives are reserving the arrow's width
    // on every tile in the project, or drawing the arrow over the icon where
    // it would sit on top of the drag source and the thumbnail. A row that is
    // occasionally one line taller is the cheapest of the three.
    if (tile.expandable) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        const bool toggled = ImGui::ArrowButton(
            "##parts", tile.expanded ? ImGuiDir_Down : ImGuiDir_Right);
        ImGui::PopStyleVar();

        if (toggled) {
            // DEFERRED, because opening a sheet rebuilds the list this row is
            // a reference into. See m_pendingExpand.
            m_pendingExpand = tile.path;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s %zu frame(s) and %zu animation(s)",
                              tile.expanded ? "Hide" : "Show", tile.sheetFrames,
                              tile.sheetAnimations);
        }
        ImGui::SameLine(0.0f, 2.0f);
    }

    // The name under the tile, wrapped to the tile's width so that one long
    // filename does not push the whole grid apart.
    ImGui::PushTextWrapPos(labelLeft + m_iconSize);
    ImGui::TextUnformatted(tile.label.c_str());
    ImGui::PopTextWrapPos();

    ImGui::EndGroup();

    // ---- where this tile ended up ----------------------------------------
    //
    // Recorded for the one job ImGui cannot help with: a file dragged in from
    // Explorer is reported by the operating system as a position, and working
    // out what was under that position means having the positions. Taken
    // after EndGroup so it covers the icon AND its label, which is what the
    // user is aiming at.
    //
    // ONLY IF IT IS ACTUALLY ON SCREEN. A tile scrolled past the bottom of
    // the grid is still submitted to ImGui and still has a position - it is
    // simply clipped - so recording it unconditionally would let a file
    // dropped near the panel's bottom edge be filed into a folder the user
    // cannot even see. That is a wrong answer delivered with confidence,
    // which is the worst kind.
    if (ImGui::IsItemVisible()) {
        const ImVec2 tileMin = ImGui::GetItemRectMin();
        const ImVec2 tileMax = ImGui::GetItemRectMax();
        m_rows.push_back(Row{tile.path, tile.IsDirectory(), tileMin.x, tileMin.y,
                             tileMax.x, tileMax.y});
    }

    // ---- and a FOLDER is a drop target -----------------------------------
    //
    // Only a folder. Dropping one file onto another has no meaning here, and
    // a target that accepts everything and acts on almost nothing is how a
    // panel starts feeling unpredictable.
    if (tile.IsDirectory()) {
        AcceptMoveInto(tile.path);
    }

    if (iconHovered) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tile.path.c_str());
        if (tile.IsDirectory()) {
            ImGui::TextDisabled("double-click to open, drag things onto it to move "
                                "them in");
        } else if (tile.IsFile()) {
            // IsFile(), NOT the absence of IsDirectory(). This is the third
            // place that reads *tile.file, and the header states that IsFile()
            // is the gate at every one of them - an "else" would be a fourth
            // rule, true only while nothing but a file and a folder reaches
            // this function.
            ImGui::TextDisabled("%llu bytes", tile.file->byteSize);
        }
        switch (kind) {
            case AssetKind::Texture:
                ImGui::TextDisabled("drag into the Scene view, or onto an entity");
                break;
            case AssetKind::Font:
                ImGui::TextDisabled("drag onto the Font field of a UI Text");
                break;
            case AssetKind::Script:
                ImGui::TextDisabled("drag onto an entity in the Hierarchy or Inspector");
                if (!eng::ScriptRegistry::IsRegistered(
                        ScriptNameFromPath(tile.path))) {
                    ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f),
                                       "not compiled into this build yet");
                }
                break;
            case AssetKind::Scene:
                ImGui::TextDisabled("double-click to open");
                break;
            case AssetKind::Tile:
                ImGui::TextDisabled("double-click to edit it in the Tile Palette");
                break;
            case AssetKind::Prefab:
                ImGui::TextDisabled("drag into the Scene view, onto an entity, or "
                                    "double-click to place one");
                break;
            case AssetKind::Unknown:
                break;
        }
        ImGui::EndTooltip();
    }

    // A SINGLE CLICK SELECTS, which is what gives the Delete key something to
    // act on. Taken on mouse-DOWN rather than release, so a click that turns
    // into a drag still selects what it started from.
    if (iconHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        Pick(tile);
    }

    // The selected tile gets a border. A selection you cannot see is worse
    // than none at all, because the Delete key would then act on something
    // invisible.
    // NOT A PART'S, even when the strings match. A real file called
    // "hero.png#f0" beside a frame of that name would otherwise draw two
    // borders for one selection - and the border is what tells somebody
    // which of the two the Delete key is aimed at.
    if (!m_pickedIsPart && m_picked == tile.path) {
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(),
                                            ImGui::GetItemRectMax(),
                                            IM_COL32(120, 190, 250, 255), 3.0f, 0, 2.0f);
    }

    DrawEntryContextMenu(tile);

    if (iconClicked) {
        if (tile.IsDirectory()) {
            // DEFERRED. Navigating refreshes, which rebuilds the vector this
            // very row is a reference into. See m_pendingNavigate.
            m_pendingNavigate = tile.path;
        } else if (kind == AssetKind::Scene) {
            // Recorded rather than loaded here - loading destroys every
            // entity, and other panels are still using them this frame. See
            // EditorState::requestedScene.
            EditorState::Get().requestedScene = tile.path;
        } else if (kind == AssetKind::Tile) {
            // A .otile is JSON, so the system editor WOULD open it - but the
            // Tile Palette can show the sheet, every one of the 47 shapes it
            // produces, and the settings as checkboxes. Opening Notepad on it
            // instead would be the least useful of the available answers.
            TilePaintState& tiles = TilePaintState::Get();
            tiles.tilePath        = tile.path;
            tiles.revealRequested = true;
        } else if (kind == AssetKind::Prefab) {
            // ==============================================================
            //  DOUBLE-CLICK PLACES ONE, which is NOT what Unity does - Unity
            //  opens the prefab in its own isolated scene view, and there is
            //  no such view here. Of the answers actually available, placing
            //  an instance is the one somebody double-clicking a prefab
            //  wants; opening its JSON in Notepad is not.
            //
            //  Creating entities mid-frame is safe, and only creating:
            //  Scene::ForEach re-reads its size each time round and the
            //  Hierarchy walks a copy of each child list. It is DESTROYING
            //  that has to wait - see PrefabRequest in EditorApp.h.
            // ==============================================================
            PlacePrefab(tile.path);
        } else {
            // EVERYTHING ELSE OPENS IN WHATEVER THE SYSTEM USES FOR IT - a
            // .cpp in your C++ editor, a .png in your image viewer. That is
            // what a double-click means everywhere else, and a panel where
            // it works on folders and scenes and does nothing at all on a
            // script reads as broken rather than as a deliberate limit.
            OpenInSystemEditor(tile.path);
        }
    }

    ImGui::PopID();
}

// ---------------------------------------------------------------------------
//  One frame or one animation of a sheet.
//
//  DELIBERATELY THE SAME SHAPE AS DrawEntry, in the same order, so that the
//  two cannot drift: the icon, the drag source, the label, the group, the
//  tooltip, the click, the border, the context menu, the double-click.
//
//  THREE THINGS IT DOES NOT DO, each for a reason worth stating:
//
//  NO Row IS RECORDED. m_rows answers one question - which FOLDER is this
//  screen position over, for a file dragged in from Explorer - and a frame is
//  not a folder and not a place anything can be written.
//
//  NO DROP TARGET OF ANY KIND. ImGui keeps the SMALLEST accepting bounding
//  box, so a target on a tile this size would beat the whole-panel entity
//  target and hand it an entity to make a prefab in a "folder" called
//  "textures/hero.png#f0" - a path that reaches fs::create_directories.
//
//  NO MOVE LINE IN THE DRAG PREVIEW. MoveLooksLegal refuses every sub-asset,
//  and it is consulted before any target lights up, so offering the gesture
//  would be a promise followed by silence.
// ---------------------------------------------------------------------------
void AssetBrowserPanel::DrawPartTile(const Tile& part) {
    ImGui::BeginGroup();

    const ImVec2 iconSize(m_iconSize, m_iconSize);

    if (part.trouble.empty()) {
        // ==================================================================
        //  AN ITEM OF THE FULL TILE SIZE, WITH THE PICTURE DRAWN INSIDE IT.
        //
        //  ImGui::Image sizes the ITEM to the image, so a letterboxed 58 x 72
        //  frame would make a 58-wide item: a ragged grid, which the
        //  placeholder square in DrawEntry exists to prevent. Dummy reserves
        //  the square and the draw list puts the picture in the middle of it.
        //  Both call ItemAdd with a null id and ImageBorderSize is zero, so
        //  the two kinds of tile are exactly the same size and the drag source
        //  below works on either.
        //
        //  LETTERBOXED, UNLIKE A FILE TILE, which is an inconsistency with a
        //  reason: a file tile's thumbnail is squashed into the square and has
        //  been for as long as this panel has existed. A frame's rectangle is
        //  arbitrary in BOTH axes - 16 x 20 here - and squashing it would
        //  misrepresent the shape of the thing about to be dropped.
        // ==================================================================
        ImGui::Dummy(iconSize);

        const ImVec2 min  = ImGui::GetItemRectMin();
        ImDrawList*  draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(min, ImVec2(min.x + m_iconSize, min.y + m_iconSize),
                            IM_COL32(16, 18, 22, 255));

        const float longest =
            static_cast<float>((part.pixelW > part.pixelH) ? part.pixelW : part.pixelH);
        const float scale = (longest > 0.0f) ? m_iconSize / longest : 1.0f;
        const float wide  = static_cast<float>(part.pixelW) * scale;
        const float high  = static_cast<float>(part.pixelH) * scale;
        const ImVec2 at(min.x + (m_iconSize - wide) * 0.5f,
                        min.y + (m_iconSize - high) * 0.5f);

        draw->AddImage(reinterpret_cast<ImTextureID>(part.texture->native), at,
                       ImVec2(at.x + wide, at.y + high), ImVec2(part.u0, part.v0),
                       ImVec2(part.u1, part.v1));
    } else {
        // A CHIP RATHER THAN A PICTURE, because there is no honest picture to
        // draw. uv past 1 samples clamped, which would smear the edge pixels
        // into something that looks like a frame.
        const ImVec4 colour = ColourFor(AssetKind::Texture);
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImVec4(colour.x * 0.35f, colour.y * 0.35f,
                                     colour.z * 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, colour);
        ImGui::Button(part.isAnimation ? "ANIM" : "FRM", iconSize);
        ImGui::PopStyleColor(2);
    }

    const bool iconHovered = ImGui::IsItemHovered();
    const bool iconClicked = iconHovered && ImGui::IsMouseDoubleClicked(0);

    // ---- and it is draggable, which is the point of all of this -----------
    //
    // kPayloadTexture, with no new payload id and no new AssetKind, because a
    // frame of an image IS an image: ClassifyAsset says so by recursing on the
    // sheet. That one line is what makes an entity, the Scene view, the
    // Inspector's whole-panel target and a UI Image's Texture field all accept
    // one with no change at all.
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload(kPayloadTexture, part.path.c_str(),
                                  part.path.size() + 1);

        ImGui::TextColored(ColourFor(AssetKind::Texture), "%s",
                           part.isAnimation ? "ANIM" : "FRM");
        ImGui::SameLine();

        // QUALIFIED, because "f0" on its own says nothing about which sheet it
        // came from and several sheets can be open at once.
        ImGui::Text("%s of %s", part.label.c_str(),
                    LeafOf(part.sheetPath).c_str());
        ImGui::TextDisabled("drop in the Scene view, or onto an entity");
        if (part.isAnimation && !part.firstFrame.empty()) {
            ImGui::TextDisabled("as an image this is its first frame, '%s'",
                                part.firstFrame.c_str());
        }
        ImGui::EndDragDropSource();
    }

    const float labelLeft = ImGui::GetCursorPos().x;
    ImGui::PushTextWrapPos(labelLeft + m_iconSize);
    ImGui::TextUnformatted(part.label.c_str());
    ImGui::PopTextWrapPos();

    ImGui::EndGroup();

    if (iconHovered) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(part.path.c_str());
        ImGui::TextDisabled("part of a spritesheet");
        if (part.isAnimation) {
            ImGui::TextDisabled("%zu frame(s) at %.1f fps, %s", part.frameCount,
                                static_cast<double>(part.fps),
                                part.loop ? "repeating" : "played once");
        }
        if (part.trouble.empty()) {
            ImGui::TextDisabled("%d x %d pixels of %s", part.pixelW, part.pixelH,
                                LeafOf(part.sheetPath).c_str());
            ImGui::TextDisabled("drag into the Scene view, or onto an entity");
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s",
                               part.trouble.c_str());
        }
        ImGui::EndTooltip();
    }

    if (iconHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        Pick(part.path, false, true);
    }

    if (m_pickedIsPart && m_picked == part.path) {
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(),
                                            ImGui::GetItemRectMax(),
                                            IM_COL32(120, 190, 250, 255), 3.0f, 2.0f);
    }

    DrawPartContextMenu(part);

    if (iconClicked) {
        // DOUBLE-CLICK SELECTS THE SHEET, because everything there is to do to
        // a frame is done to the sheet: its rectangle, its name and its
        // animations are all in the slicer. Opening the image viewer on the
        // whole sheet would be the least useful of the available answers.
        Pick(part.sheetPath, false);
    }
}

// ---------------------------------------------------------------------------
//  Right-clicking a part. Four of the five usual items have no meaning here.
// ---------------------------------------------------------------------------
void AssetBrowserPanel::DrawPartContextMenu(const Tile& part) {
    if (!ImGui::BeginPopupContextItem("part-actions")) {
        return;
    }

    // Selects first, for the same reason DrawEntryContextMenu does.
    Pick(part.path, false, true);

    ImGui::TextDisabled("%s", part.path.c_str());
    ImGui::Separator();

    if (ImGui::MenuItem("Select the sheet")) {
        Pick(part.sheetPath, false);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shows the slicer in the Inspector, which is where a\n"
                          "frame is renamed, moved or removed.");
    }

    if (ImGui::MenuItem("View in File Explorer")) {
        // THE SHEET, because that is the file. A frame has no path on disk for
        // the operating system to be shown.
        RevealAsset(part.sheetPath);
    }

    // ---- one frame, as a tile ---------------------------------------------
    //
    // THE GESTURE THIS WHOLE FEATURE EXISTS FOR. A tile drawn from one picture
    // is exactly what a frame is, and the frame needs no file of its own: the
    // .otile names "textures/atlas.png#rock" and the sheet stays one image on
    // disk. See TileAsset.h on the two kinds of tile art.
    //
    // OFFERED ONLY FOR A FRAME. An animation is several pictures and a tile is
    // one, so the item is left out rather than written and then refused - and
    // a frame whose rectangle does not fit its sheet cannot draw at all.
    if (!part.isAnimation && part.trouble.empty()) {
        if (ImGui::MenuItem("Make a Tile from this frame...")) {
            // THE NEW TILE DIALOG, pre-filled - not a silent write. It is
            // where the name is chosen, where the engine's own validation runs
            // live, and where the tile is shown before it exists. A one-click
            // version would have to invent a name and a folder.
            std::snprintf(m_newTileArt, sizeof(m_newTileArt), "%s", part.path.c_str());
            std::snprintf(m_newTileName, sizeof(m_newTileName), "%s",
                          part.label.c_str());
            m_newTileAsImage = true;
            m_openNewTile    = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Writes a .otile that draws this one frame. The frame\n"
                              "stays part of its sheet - nothing is copied out.");
        }
    }

    ImGui::Separator();

    // NO Open File, NO Rename, NO Delete - said rather than left out, because
    // a menu that is simply shorter reads as a menu that has failed.
    ImGui::TextDisabled("a frame is not a file: rename or remove it");
    ImGui::TextDisabled("in the Inspector's Frames list");

    ImGui::EndPopup();
}

void AssetBrowserPanel::DrawEntryContextMenu(const Tile& tile) {
    // BeginPopupContextItem opens on a right-click of the PREVIOUS item, so
    // this has to come straight after the tile's group.
    if (!ImGui::BeginPopupContextItem("asset-actions")) {
        return;
    }

    // Right-clicking selects first. Without this the menu would act on
    // whatever was selected BEFORE, so right-clicking one file and choosing
    // Delete would delete a different one - about as bad as a context menu
    // can get.
    Pick(tile);

    ImGui::TextDisabled("%s", tile.label.c_str());
    ImGui::Separator();

    if (tile.IsDirectory()) {
        if (ImGui::MenuItem("Open Folder")) {
            m_pendingNavigate = tile.path;   // see m_pendingNavigate
        }
    } else if (tile.kind == AssetKind::Prefab) {
        if (ImGui::MenuItem("Place in Scene")) {
            PlacePrefab(tile.path);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Puts an instance in the middle of the Scene view. Drag "
                              "it from here instead to choose where it lands.");
        }
        if (ImGui::MenuItem("Open File")) {
            OpenInSystemEditor(tile.path);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Opens the prefab's JSON in a text editor. Edits arrive "
                              "in the open scene when you come back to the editor.");
        }
    } else if (tile.kind == AssetKind::Scene) {
        // A scene opens IN THE EDITOR, because that is obviously what you
        // want - the system would hand it to a text editor.
        if (ImGui::MenuItem("Open Scene")) {
            EditorState::Get().requestedScene = tile.path;
        }
        if (ImGui::MenuItem("Open File")) {
            OpenInSystemEditor(tile.path);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Opens the scene's JSON in a text editor, if this "
                              "machine has one set up for .oscene files.");
        }
    } else {
        if (ImGui::MenuItem("Open File")) {
            OpenInSystemEditor(tile.path);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Opens it in whatever this machine uses for that kind "
                              "of file - the same as double-clicking it.");
        }

        // The same offer as on a frame, for a whole picture. Both land in the
        // New Tile dialog with the kind already chosen, because an ordinary
        // image is an "image" tile and guessing "sheet" is what used to send
        // people to a screen of scrambled quarters. See TileAsset.h.
        if (tile.kind == AssetKind::Texture) {
            if (ImGui::MenuItem("Make a Tile from this image...")) {
                std::snprintf(m_newTileArt, sizeof(m_newTileArt), "%s",
                              tile.path.c_str());
                std::snprintf(m_newTileName, sizeof(m_newTileName), "%s",
                              tile.label.c_str());
                m_newTileAsImage = true;
                m_openNewTile    = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("One picture, drawn whole in every cell. For art "
                                  "that\njoins up with its neighbours, choose Sheet "
                                  "in the dialog.");
            }
        }
    }

    if (ImGui::MenuItem("View in File Explorer")) {
        RevealAsset(tile.path);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Always works, even for file types nothing is set up to "
                          "open.");
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Rename...", "F2")) {
        RequestRename(tile);
    }
    if (ImGui::MenuItem("Delete...", "Del")) {
        RequestDelete(tile);
    }

    ImGui::EndPopup();
}

void AssetBrowserPanel::OpenInSystemEditor(const std::string& virtualPath) {
    std::string error;
    if (OpenExternally(virtualPath, error)) {
        m_statusIsError = false;
        std::snprintf(m_status, sizeof(m_status), "opened %s", virtualPath.c_str());
        return;
    }

    // REPORTED TWICE, AND AT ERROR RATHER THAN WARNING.
    //
    // This is the whole fix for "double-clicking a file does nothing": the
    // project's log threshold throws warnings away, so a warning here would
    // reach neither the Console nor the log file. See ShellOpen.h.
    m_statusIsError = true;
    std::snprintf(m_status, sizeof(m_status), "%s", error.c_str());
    ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not open '{}': {}", virtualPath,
                     error);
}

void AssetBrowserPanel::RevealAsset(const std::string& virtualPath) {
    std::string error;
    if (RevealInFolder(virtualPath, error)) {
        m_statusIsError = false;
        std::snprintf(m_status, sizeof(m_status), "showed %s in the file explorer",
                      virtualPath.c_str());
        return;
    }
    m_statusIsError = true;
    std::snprintf(m_status, sizeof(m_status), "%s", error.c_str());
    ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not show '{}': {}", virtualPath,
                     error);
}

// ---------------------------------------------------------------------------
//  May this row be renamed or deleted? Said once, in words, for a person.
//
//  THE REFUSAL IS A REDIRECTION, not a dead end. A frame IS renamed and IS
//  removed - in the Inspector's Frames list, which also rewrites every
//  animation that used the old name. So this says where to go.
// ---------------------------------------------------------------------------
bool AssetBrowserPanel::RefuseIfNotAFile(const Tile& tile) {
    if (tile.IsFile()) {
        return true;
    }
    m_statusIsError = true;
    std::snprintf(m_status, sizeof(m_status),
                  "'%s' is one frame of %s, not a file - rename or remove it in "
                  "the Inspector's Frames list",
                  eng::SubAssetNameOf(tile.path).c_str(),
                  eng::SheetPathOf(tile.path).c_str());
    return false;
}

void AssetBrowserPanel::Pick(const Tile& tile) {
    Pick(tile.path, tile.IsDirectory());
}

void AssetBrowserPanel::Pick(const std::string& virtualPath, bool isDirectory,
                             bool isPart) {
    m_picked       = virtualPath;
    m_pickedIsPart = isPart;

    // A FOLDER IS NOT SOMETHING THE INSPECTOR CAN DESCRIBE, so it is not told
    // about one. See the note on m_picked in the header for what went wrong
    // when it was.
    //
    // A FRAME OF A SHEET IS, and that is the third case this now has. It is
    // not a file either, but the Inspector has something to say about one -
    // which rectangle of which image it is - so it is told.
    if (isDirectory) {
        EditorState::Get().selectedAsset.clear();
    } else {
        EditorState::Get().selectedAsset = virtualPath;
    }
}

void AssetBrowserPanel::RequestRename(const Tile& tile) {
    if (!RefuseIfNotAFile(tile)) {
        return;
    }
    eng::PrefabLibrary::Forget(tile.path);
    m_renamePath      = tile.path;
    m_renameOriginal  = tile.label;
    m_openRenamePopup = true;
    std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", tile.label.c_str());

    // Read as the dialog is requested, for the reason the New Script popup
    // reads it as the dialog opens.
    m_scriptClasses = ScriptBuild::GatherScriptClasses();
}

void AssetBrowserPanel::RequestDelete(const Tile& tile) {
    if (!RefuseIfNotAFile(tile)) {
        return;
    }
    const eng::FileSystem::DirEntry& entry = *tile.file;

    // THE PARSED COPY GOES NOW, before the file does. PrefabLibrary is a cache
    // with nothing watching the disk, so a prefab that has been deleted,
    // renamed or moved would otherwise go on being handed out from memory -
    // and instances of it would keep rebuilding from a file that is not there.
    // Harmless to call for a file that is not a prefab.
    eng::PrefabLibrary::Forget(entry.virtualPath);
    m_deletePath        = entry.virtualPath;
    m_deleteName        = entry.name;
    m_deleteIsDirectory = entry.isDirectory;
    m_openDeletePopup   = true;
    m_deleteCount       = 0;

    // HOW MUCH IS IN THERE, counted up front so the question can be specific.
    // "Delete this folder and the 23 files in it?" can be answered; "are you
    // sure?" cannot.
    if (entry.isDirectory) {
        std::error_code ec;
        const fs::path  real = eng::FileSystem::Resolve(entry.virtualPath);
        for (const auto& item : fs::recursive_directory_iterator(real, ec)) {
            if (item.is_directory(ec)) {
                continue;
            }
            // SIDECARS ARE NOT COUNTED, BECAUSE THEY ARE NOT SHOWN. The whole
            // point of the number is that the question can be answered, and
            // "the 46 file(s) inside it" over a folder displaying 23 is a
            // question that makes somebody cancel and go looking for the
            // other 23. They are still deleted; they are in the folder.
            //
            // Compared as a path rather than through HasExtension so that a
            // name this engine cannot represent in the local code page cannot
            // throw out of a confirmation dialog's file count. The cost is
            // that a ".OIMPORT" would be counted, which is off by one.
            if (item.path().extension() == fs::path(eng::kImportExtension)) {
                continue;
            }
            ++m_deleteCount;
        }
    }
}

void AssetBrowserPanel::DrawRenamePopup() {
    if (m_openRenamePopup) {
        ImGui::OpenPopup("Rename Asset");
        m_openRenamePopup = false;
    }

    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal("Rename Asset", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::Text("Rename '%s' to:", m_renameOriginal.c_str());

    // THE PATH AS WELL AS THE NAME. Two lines, and the only thing that
    // would make a dialog naming one thing and acting on another visible to
    // the person pressing the button.
    ImGui::TextDisabled("%s", m_renamePath.c_str());

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(320.0f);
    const bool submitted =
        ImGui::InputText("##assetname", m_renameBuffer, sizeof(m_renameBuffer),
                         ImGuiInputTextFlags_EnterReturnsTrue);

    // ---- renaming a script is not just a file operation ------------------
    //
    // Said BEFORE it happens rather than discovered afterwards. The class
    // inside a .cpp keeps its old name, and the scene refers to the CLASS -
    // so renaming the file alone changes nothing about what runs.
    if (ClassifyAsset(m_renamePath) == AssetKind::Script) {
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                           "This only renames the FILE. The class inside it keeps its");
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                           "name, and that is what scenes refer to.");
    }

    // ---- and a script may not be renamed onto another script's name ------
    //
    // THE FILE STEM IS THE SCRIPT'S NAME as far as everything outside the
    // file is concerned: AssetDragDrop::ScriptNameFromPath attaches a script
    // by it, and the new-script template writes the stem into `class {0}` and
    // into ENGINE_REGISTER_SCRIPT. So a stem that another script already owns
    // is refused even though renaming the file does not itself rewrite the
    // class - which is what the amber note above says, and is why the two
    // things are worth keeping in step rather than letting them drift.
    //
    // IsValidScriptName is checked here too. It was not before, so
    // `Bullet.cpp` could be renamed to `9 rocket!.cpp` - a name no class could
    // ever have.
    std::string scriptRefusal;
    if (ClassifyAsset(m_renamePath) == AssetKind::Script) {
        const std::string stem = fs::path(m_renameBuffer).stem().string();
        std::string       why;
        if (!stem.empty() && !IsValidScriptName(stem, why)) {
            scriptRefusal = why;
        } else if (const std::string* taken = ScriptNameTaken(stem, m_renamePath)) {
            scriptRefusal = "'" + stem + "' is already the script in '" + *taken +
                            "' - two scripts cannot share a name, so only one of "
                            "them could ever run";
        }
        if (!scriptRefusal.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s",
                               scriptRefusal.c_str());
        }
    }

    const bool blank = m_renameBuffer[0] == '\0';
    ImGui::BeginDisabled(blank || !scriptRefusal.empty());
    if ((submitted && !blank) || ImGui::Button("Rename", ImVec2(120, 0))) {
        std::string error;
        if (eng::FileSystem::Rename(m_renamePath, m_renameBuffer, error)) {
            m_statusIsError = false;
            std::snprintf(m_status, sizeof(m_status), "renamed to %s", m_renameBuffer);

            // THE IMPORT SETTINGS COME WITH IT. Built from the file-local
            // helpers rather than by concatenation, because the assets root IS
            // the empty virtual path and JoinPath is what encodes that - ""
            // followed by "/" followed by a name begins with a slash, which
            // means something else entirely.
            //
            // A FOLDER NEEDS NO FLAG HERE, which is why m_renamePath carries
            // no isDirectory the way m_deletePath does: FollowAsset looks for
            // "<old>.oimport", a folder has none, and it does nothing.
            eng::ImportLibrary::FollowAsset(
                m_renamePath, JoinPath(ParentOf(m_renamePath), m_renameBuffer));

            m_picked.clear();
            EditorState::Get().selectedAsset.clear();
            Refresh();
        } else {
            m_statusIsError = true;
            std::snprintf(m_status, sizeof(m_status), "%s", error.c_str());
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not rename: {}", error);
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void AssetBrowserPanel::DrawDeletePopup() {
    if (m_openDeletePopup) {
        ImGui::OpenPopup("Delete Asset");
        m_openDeletePopup = false;
    }

    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal("Delete Asset", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    // ALWAYS ASKS, and this is the one dialog in the editor that genuinely
    // has to. Deleting an entity goes through the undo stack - which, as of
    // editor/src/UndoStack.h, actually exists rather than being the plan this
    // comment described. Deleting a FILE does not and cannot: it is gone from
    // the disk, and no snapshot of the scene reaches it.
    if (m_deleteIsDirectory) {
        ImGui::Text("Delete the folder '%s'", m_deleteName.c_str());
        ImGui::Text("and the %zu file(s) inside it?", m_deleteCount);
    } else {
        ImGui::Text("Delete '%s'?", m_deleteName.c_str());
    }
    ImGui::TextDisabled("%s", m_deletePath.c_str());   // see DrawRenamePopup

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f),
                       "This cannot be undone - Ctrl+Z does not reach the disk.");

    if (ClassifyAsset(m_deletePath) == AssetKind::Script) {
        ImGui::TextDisabled("Any entity using this script will show it as missing.");
    } else if (ClassifyAsset(m_deletePath) == AssetKind::Texture) {
        ImGui::TextDisabled("Any sprite using it will draw the missing-texture square.");
    }

    ImGui::Separator();

    if (ImGui::Button("Delete", ImVec2(120, 0))) {
        std::size_t removed = 0;
        std::string error;
        if (eng::FileSystem::Remove(m_deletePath, removed, error)) {
            m_statusIsError = false;
            std::snprintf(m_status, sizeof(m_status), "deleted %s (%zu item(s))",
                          m_deleteName.c_str(), removed);
            ENGINE_LOG_INFO(eng::Channels::kEditor, "deleted '{}' ({} item(s))",
                            m_deletePath, removed);

            // An empty new path means "gone", so the settings go with it. For
            // a FOLDER this finds nothing and does nothing, because the
            // sidecars inside it have already gone with the folder.
            eng::ImportLibrary::FollowAsset(m_deletePath, "");

            m_picked.clear();
            EditorState::Get().selectedAsset.clear();
            Refresh();
        } else {
            m_statusIsError = true;
            std::snprintf(m_status, sizeof(m_status), "%s", error.c_str());
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not delete: {}", error);
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void AssetBrowserPanel::Draw() {
    // ========================================================================
    //  EVERYTHING HERE COMES FIRST, before anything reads m_tiles.
    //
    //  Each one either changes what is in the folder or lets go of something a
    //  row is holding, and each is discovered at a moment when that list is
    //  already being walked - a move half way through the tile loop, a delete
    //  confirmed from inside a modal, an import from an event that arrived
    //  before the frame began. Doing the work here, at the top, is the same
    //  deferral the engine makes for destroying an entity and the editor makes
    //  for loading a scene: find out now, act at a point where nothing is
    //  mid-sentence.
    //
    //  THE TOP OF Draw IS ALSO THE ONLY SAFE PLACE TO RELEASE A TEXTURE,
    //  because nothing has been submitted to this frame's draw list yet. That
    //  is why m_retired is emptied here.
    // ========================================================================
    // The rows a refresh replaced, whose textures last frame's draw list was
    // still holding. See m_retired - this is the line that stops a delete
    // from drawing with freed memory.
    m_retired.clear();

    ApplyPendingMove();
    ApplyPendingNavigate();
    ApplyPendingExpand();

    // ======================================================================
    //  SOMEBODY ELSE CHANGED WHAT AN ASSET IS.
    //
    //  Slicing a sheet in the Inspector changes whether its tile has an
    //  arrow and what is underneath it, and this panel's listing is cached
    //  on purpose. Without this the arrow would appear only when somebody
    //  next navigated or pressed Refresh, which reads as the slicer not
    //  having worked. See EditorState::assetsNeedRefresh.
    //
    //  IT STAYS RAISED WHILE THIS PANEL IS HIDDEN, and that is the useful
    //  behaviour rather than a gap. ImGui does not call Draw for a panel
    //  nobody can see, so a sheet sliced while the Assets tab sits behind the
    //  Console is picked up on the first frame the tab is brought forward.
    //  Raising it twice costs nothing: it is a flag, so any number of slices
    //  made behind this panel's back come to one refresh.
    // ======================================================================
    if (EditorState::Get().assetsNeedRefresh) {
        EditorState::Get().assetsNeedRefresh = false;
        Refresh();
    }

    ImportDroppedFiles();
    RevealRequestedAsset();

    DrawBreadcrumb();
    ImGui::Separator();

    if (!m_valid) {
        // A folder can legitimately be missing - it may have been deleted from
        // outside the editor while you were looking at it - so this is an
        // instruction rather than an error.
        ImGui::TextDisabled("'%s' does not exist yet.", DisplayPath(m_directory).c_str());
        ImGui::TextDisabled("Press Up to go back, or Refresh.");
        DrawNewScriptPopup();
        DrawNewTilePopup();
        DrawNewFolderPopup();
        return;
    }

    ImGui::BeginChild("##grid", ImVec2(0, 0), ImGuiChildFlags_None);

    // AFTER BeginChild, so the width it measures is the one the tiles are
    // actually laid out in. See the note on FlowGrid.
    FlowGrid grid(m_iconSize);

    // Rebuilt from scratch every frame, because a scroll, a resize or a
    // change to the icon slider moves every tile. A half-updated list of
    // positions is worse than none: it would send a dropped file confidently
    // into the wrong folder.
    m_rows.clear();

    for (std::size_t i = 0; i < m_tiles.size(); ++i) {
        const Tile& tile = m_tiles[i];

        // An open sheet starts its own row, so that the run underneath it
        // reads as belonging to it rather than to whatever it happened to
        // follow.
        if (tile.expanded) {
            grid.Break();
        }
        grid.Next();
        DrawEntry(tile, static_cast<int>(i));

        if (!tile.expanded) {
            continue;
        }

        // ==================================================================
        //  THE RUN OF PARTS, drawn here rather than in a member function of
        //  its own, because the grid is this function's to own: FlowGrid is
        //  file-local by design and a member taking one would have to be
        //  declared in a header that has no business knowing about it.
        //
        //  A NESTED id SCOPE, and it has to be nested rather than a running
        //  ordinal: two rows CAN carry the same path - a real file named
        //  "hero.png#f0" beside a frame of that name - while an index is
        //  unique by construction. Without it, part 0 of the third sheet could
        //  hash to the same id as another tile and the two would become one
        //  item, with one drag source and one context menu between them.
        //
        //  PARTS ARE SIBLINGS of the sheet's tile, drawn after DrawEntry has
        //  popped its own id. Never between the sheet's EndGroup and its
        //  context menu, which binds to the last item submitted - see the note
        //  on DrawEntryContextMenu.
        // ==================================================================
        grid.SetIndent(ImGui::GetStyle().IndentSpacing);

        ImGui::PushID(static_cast<int>(i));
        ImGui::PushID("parts");
        for (std::size_t p = 0; p < tile.parts.size(); ++p) {
            grid.Next();
            ImGui::PushID(static_cast<int>(p));
            DrawPartTile(tile.parts[p]);
            ImGui::PopID();
        }
        if (tile.parts.empty()) {
            ImGui::TextDisabled("nothing in this sheet could be cut - see the log");
        }
        ImGui::PopID();
        ImGui::PopID();

        grid.SetIndent(0.0f);
    }

    // Back to no indent, in case the last row drawn was a run of parts.
    grid.SetIndent(0.0f);
    if (m_tiles.empty()) {
        ImGui::TextDisabled("this folder is empty");
    }

    // ---- the keys this panel owns ---------------------------------------
    //
    // Checked INSIDE the grid's child window, because that is what has focus
    // when you have just clicked a tile.
    //
    // Three guards, each earning its place. The panel must be FOCUSED, or
    // Delete pressed while working in the Inspector would delete a file.
    // WantTextInput must be false, or Delete while typing in the rename box
    // would delete the thing being renamed - which is a genuinely alarming
    // thing for a rename box to do. And something must actually be selected.
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !io.WantTextInput && !m_picked.empty()) {

        // ==================================================================
        //  A PART IS NOT LOOKED UP AT ALL, AND THAT IS NOT BELT AND BRACES.
        //
        //  Nesting the parts inside their sheet's row keeps them out of this
        //  search, which was supposed to be enough. It is not: the search
        //  compares PATHS, and a real file can be called "hero.png#f0" and sit
        //  beside the frame of that name. Searching for a picked frame then
        //  found the real file, and the dialog that opened named the frame's
        //  own path while aiming at the image.
        //
        //  So the search is skipped entirely for a part, on the strength of
        //  what was clicked rather than what it is called. The keys still work
        //  on a real file whose name contains a hash, because that file's row
        //  is not a part.
        // ==================================================================
        const auto selected =
            m_pickedIsPart
                ? m_tiles.end()
                : std::find_if(m_tiles.begin(), m_tiles.end(), [&](const Tile& t) {
                      return t.path == m_picked && t.IsFile();
                  });

        if (selected != m_tiles.end()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                RequestDelete(*selected);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
                RequestRename(*selected);
            }
        } else if (m_pickedIsPart &&
                   (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
                    ImGui::IsKeyPressed(ImGuiKey_F2, false))) {
            // ==============================================================
            //  A FRAME IS PICKED, AND NEITHER KEY MEANS ANYTHING FOR ONE.
            //
            //  The search above finds nothing, so without this both keys do
            //  nothing at all - with a border round the tile and the panel
            //  focused, which is indistinguishable from a lost keypress. Say
            //  so, and say where the gesture does live.
            //
            //  AFTER THE SEARCH AND NOT BEFORE IT, which is the whole reason
            //  this is an else-if. A real file can be named "hero.png#f0":
            //  ListDirectory lists it and eng::IsSubAsset calls it a frame. A
            //  guard placed first would refuse Delete and F2 on a file that is
            //  genuinely there and genuinely deletable. Checking the drawn
            //  list first means the keys keep working on anything real, and
            //  this arm speaks only for something that is not.
            // ==============================================================
            m_statusIsError = true;
            std::snprintf(m_status, sizeof(m_status),
                          "'%s' is one frame of %s, not a file - rename or remove "
                          "it in the Inspector's Frames list",
                          eng::SubAssetNameOf(m_picked).c_str(),
                          eng::SheetPathOf(m_picked).c_str());
        } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
                   ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
            // AND THE LAST CASE SPEAKS TOO: something is selected, it is not a
            // part, and it is not in the folder being shown - the usual way
            // being that you clicked a file and then opened a folder. These
            // keys may only act on what is in front of you, which is the gate
            // above; before this arm they simply did nothing, and a key that
            // does nothing is indistinguishable from a key that was missed.
            m_statusIsError = true;
            std::snprintf(m_status, sizeof(m_status),
                          "'%s' is not in the folder you are looking at - open "
                          "its folder to rename or delete it", m_picked.c_str());
        }
        // NO `else` THAT CLEARS THE SELECTION, and that is a deliberate
        // removal rather than an oversight. It used to drop a selection that
        // was not in this folder's listing, which was right while the string
        // belonged to this panel and is wrong now that the Inspector is
        // showing it: navigating to another folder would blank the Inspector,
        // and so would a frame on which this panel happened to have focus.
        //
        // The INSPECTOR'S idea of the selection is dropped when the file goes,
        // by EditorState::PruneSelection, which runs every frame whatever has
        // focus - the thing this branch could never do.
        //
        // THIS PANEL'S OWN m_picked IS NOT PRUNED ANYWHERE, and saying so
        // matters because the sentence above used to imply it was. Delete a
        // selected file from Explorer and m_picked still names it until
        // something refreshes: the border stays, and Delete opens the
        // confirmation for a file that has already gone, which FileSystem
        // then refuses by name. Visible and recoverable, so it is left as a
        // known limit rather than given a per-frame disk check of its own.
        //
        // The find_if above stays exactly as it was, because it is still the
        // right gate for the two KEYS: Delete and F2 may only act on something
        // in the folder you are looking at.
    }

    ImGui::EndChild();

    // AFTER the grid, so m_rows holds this frame's positions and the outline
    // round the folder under the cursor is drawn over the tile rather than
    // under it.
    // ======================================================================
    //  THE WHOLE PANEL TAKES AN ENTITY, not only its folder tiles.
    //
    //  Dragging something out of the Hierarchy and having to land it precisely
    //  on a folder icon - when the folder you want is usually the one already
    //  open, which has no icon of its own - would make the gesture a game of
    //  darts. Dropped anywhere else, it goes into the folder being shown.
    //
    //  AFTER the tiles, so that a folder directly under the cursor still wins -
    //  though NOT for the reason this comment used to give. ImGui does not
    //  prefer the most recently opened target; it keeps the one with the
    //  SMALLEST bounding box ("Accept smallest drag target bounding box, this
    //  allows us to nest drag targets conveniently without ordering
    //  constraints"). The outcome is the same here, because a folder tile is
    //  smaller than the window - but the reason matters: a reader who trusted
    //  the old sentence would conclude that giving a small tile a drop target
    //  cannot steal this one, when it is exactly what would happen.
    // ======================================================================
    if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->Rect(),
                                         ImGui::GetID("##assets_entity_drop"))) {
        AcceptEntityDropAsPrefab(m_directory);
        ImGui::EndDragDropTarget();
    }

    DrawImportHighlight();

    DrawNewScriptPopup();
    DrawNewTilePopup();
    DrawNewFolderPopup();

    // OPENED FROM HERE, not from inside a tile. A modal opened while that
    // tile's PushID is active belongs to that id, and simply never appears.
    DrawRenamePopup();
    DrawDeletePopup();

    if (m_status[0] != '\0') {
        ImGui::Separator();

        // A FAILURE IS NOT GREY. The ordinary "created X" note is fine dim -
        // it confirms something you watched happen. A refusal has to catch
        // the eye, or it is only technically being reported, which is how
        // "files cannot be opened" went unexplained in the first place.
        if (m_statusIsError) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.32f, 1.0f), "%s", m_status);
            ImGui::PopTextWrapPos();
            if (ImGui::SmallButton("Dismiss")) {
                m_status[0]     = '\0';
                m_statusIsError = false;
            }
        } else {
            ImGui::TextDisabled("%s", m_status);
        }
    }
}

const std::string* AssetBrowserPanel::ScriptNameTaken(const std::string& name,
                                                      const std::string& ignorePath) const {
    const auto it = m_scriptClasses.find(name);
    if (it == m_scriptClasses.end() || it->second == ignorePath) {
        return nullptr;
    }
    return &it->second;
}

bool AssetBrowserPanel::CreateScript(const std::string& name, std::string& outError) {
    if (!IsValidScriptName(name, outError)) {
        return false;
    }

    // ---- THE GATE, and it is asked again here on purpose -----------------
    //
    // The popup greys Create out while the name clashes, which is what the
    // person sees - but this function is the one that writes the file, so it
    // is the one that must not be talked past. Read fresh rather than from
    // m_scriptClasses: a dialog can sit open while something else adds a
    // script, and a stale map is exactly the case where a refusal matters.
    {
        const std::map<std::string, std::string> classes =
            ScriptBuild::GatherScriptClasses();
        const auto it = classes.find(name);
        if (it != classes.end()) {
            outError = "'" + name + "' is already the script in '" + it->second +
                       "' - two files cannot both define one class, so only one of "
                       "them could ever run";
            return false;
        }
    }

    // Into WHICHEVER FOLDER IS BEING BROWSED. The build walks all of assets/,
    // so a script is found wherever you put it - which means the right place
    // for a new one is where you were already looking, next to the scene and
    // the images it belongs with.
    const std::string path = JoinPath(m_directory, name + ".cpp");

    if (eng::FileSystem::Exists(path)) {
        outError = "'" + path + "' already exists - overwriting it would destroy "
                                "whatever is in it";
        return false;
    }

    const std::string text = DefaultScriptText(name);
    if (!eng::FileSystem::WriteTextFile(path, text, outError)) {
        return false;
    }

    // A new script is compiled a few lines below, so this counts as a compile
    // like any other. Emptied up here rather than beside that build, so that
    // the "created" line below and the build's own output are what the Console
    // is left holding.
    ClearConsoleForCompile();

    ENGINE_LOG_INFO(eng::Channels::kEditor, "created the script '{}'", path);

    // Compiled AND registered straight away, rather than waiting for the next
    // time the window regains focus.
    //
    // A script you cannot attach to anything until some unexplained later
    // moment is a script that appears not to work. Building it now means the
    // class exists by the time you have finished reading the file, and it
    // turns up in the Inspector's script list immediately.
    // SYNCHRONOUS here, on purpose. The point of building after writing a
    // new script is that the class exists by the time you have finished
    // reading the file - so this one waits. It is also the only build the
    // user did not start themselves, and a brand new file compiles fast.
    const ScriptBuild::Result build = ScriptBuild::BuildAndReload();

    if (!build.ok) {
        // Not a failure of CreateScript - the file was written and is fine.
        // The template compiles, so a failure here is almost always another
        // script that was already broken.
        ENGINE_LOG_WARN(eng::Channels::kEditor,
                        "'{}' was created, but the project's scripts did not build: {}",
                        path, build.summary);
    }
    return true;
}

void AssetBrowserPanel::DrawNewScriptPopup() {
    if (m_openNewScript) {
        ImGui::OpenPopup("New Script");
        m_openNewScript = false;

        // Read ONCE, as the dialog opens. GatherScriptClasses walks all of
        // assets/ and reads every source, which is nothing to do per frame
        // while somebody types.
        m_scriptClasses = ScriptBuild::GatherScriptClasses();
    }

    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("New Script", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("Script name");
    ImGui::SetNextItemWidth(320.0f);
    const bool submitted =
        ImGui::InputText("##scriptname", m_newScriptName, sizeof(m_newScriptName),
                         ImGuiInputTextFlags_EnterReturnsTrue);

    // The name is checked AS IT IS TYPED rather than when Create is pressed.
    // Telling somebody their name is illegal only after they commit to it is a
    // worse experience than greying the button out while they can still see
    // the reason.
    std::string       error;
    const bool        nameOk = IsValidScriptName(m_newScriptName, error);
    const std::string path = JoinPath(m_directory, std::string(m_newScriptName) + ".cpp");

    // Checked as it is typed for the same reason the name itself is: a clash
    // discovered when Create is pressed would be reported into m_status, which
    // is drawn BEHIND this still-open modal.
    const std::string* taken = ScriptNameTaken(m_newScriptName, std::string());

    if (!nameOk) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", error.c_str());
    } else if (taken != nullptr) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                           "'%s' is already the script in '%s'", m_newScriptName,
                           taken->c_str());
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                           "Two files cannot both define one class.");
    } else {
        ImGui::TextDisabled("writes assets/%s", path.c_str());
    }

    ImGui::Separator();
    ImGui::TextWrapped("The file is created from a template with the lifecycle hooks "
                       "written out and explained, and it is compiled and loaded "
                       "straight away - so you can attach it to an entity as soon as "
                       "this window closes.");
    ImGui::Separator();

    ImGui::BeginDisabled(!nameOk || taken != nullptr);
    if (submitted || ImGui::Button("Create", ImVec2(120, 0))) {
        std::string createError;
        if (CreateScript(m_newScriptName, createError)) {
            std::snprintf(m_status, sizeof(m_status), "created and built %s",
                          path.c_str());
            Refresh();
            ImGui::CloseCurrentPopup();
        } else {
            std::snprintf(m_status, sizeof(m_status), "%s", createError.c_str());
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "{}", createError);
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
//  New Tile
//
//  THE VALIDATION HERE IS THE LOADER'S OWN, not a second copy of it. The
//  dialog builds the .otile it is about to write and hands it to a scratch
//  TileAsset::Deserialize - so whatever the dialog accepts is exactly what the
//  engine will accept, and the derived tile size it shows is the one the
//  engine derived. A separate "is this image the right shape" check here would
//  be a second description of the same rule, and the two would drift.
// ---------------------------------------------------------------------------
bool AssetBrowserPanel::CreateTile(const std::string& name, const std::string& art,
                                   bool solid, bool asImage, std::string& outError) {
    if (name.empty()) {
        outError = "a tile needs a name";
        return false;
    }

    const std::string path = JoinPath(m_directory, name + ".otile");
    if (eng::FileSystem::Exists(path)) {
        outError = "'" + path + "' already exists";
        return false;
    }

    // Written WITHOUT tileSize or frames. They are measured from the image, so
    // stating them would be a second description of something the file already
    // carries - and a sheet later resized would then disagree with its own
    // tile rather than simply being re-measured. See TileAsset.h.
    eng::Json node = eng::Json::object();
    if (asImage) {
        node["_comment"] = "A tile material drawn from ONE PICTURE: every cell of it "
                           "looks the same. Extra frames go sideways, as squares. "
                           "tileSize is measured from the image. Use \"sheet\" "
                           "instead for art that joins up with its neighbours.";
        node["image"]    = art;
    } else {
        node["_comment"] = "A tile material. The sheet is ONE TILE WIDE and FIVE TILES "
                           "TALL: outer corner / vertical edge / horizontal edge / "
                           "inner corner / fill, top to bottom. Extra frames go "
                           "sideways. tileSize and frames are measured from the image.";
        node["sheet"]    = art;
    }
    node["collision"] = solid ? "Grid" : "None";

    const std::string text = node.dump(2) + "\n";
    if (!eng::FileSystem::WriteTextFile(path, text, outError)) {
        return false;
    }

    ENGINE_LOG_INFO(eng::Channels::kEditor, "created the tile '{}'", path);

    // Chosen as the brush straight away, and the Tile Palette asked to show
    // it. A new tile you then have to go and find is a new tile you have to
    // go and find.
    TilePaintState& tiles = TilePaintState::Get();
    tiles.tilePath        = path;
    tiles.revealRequested = true;
    return true;
}

void AssetBrowserPanel::DrawNewTilePopup() {
    if (m_openNewTile) {
        ImGui::OpenPopup("New Tile");
        m_openNewTile = false;
    }

    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("New Tile", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    // ---- which of the two kinds of tile this is ---------------------------
    //
    // ASKED FIRST, because it changes what everything below means: which key
    // is written, what shape the art has to be, and what the preview shows.
    // In a .otile the key IS the kind - see TileAsset.h.
    {
        int kind = m_newTileAsImage ? 1 : 0;
        ImGui::SetNextItemWidth(420.0f);
        if (ImGui::Combo("##tilekind", &kind,
                         "Sheet - five bands, joins up with its neighbours\0"
                         "Image - one picture, every cell the same\0")) {
            m_newTileAsImage = (kind == 1);
        }
        ImGui::TextDisabled(m_newTileAsImage
                                ? "a crate, a bush, a sign - or one frame of a sheet"
                                : "ground, water, walls - anything that forms a surface");
    }

    ImGui::Spacing();
    ImGui::TextUnformatted(m_newTileAsImage ? "Picture" : "Tile sheet");
    ImGui::SetNextItemWidth(420.0f);
    ImGui::InputText("##tileart", m_newTileArt, sizeof(m_newTileArt));
    ImGui::TextDisabled(m_newTileAsImage
                            ? "a path under assets/, or one frame of a sliced sheet"
                            : "a path under assets/, e.g. textures/tiles/stone.png");

    // Every image in the folder being browsed, as a shortcut past typing one.
    if (ImGui::BeginCombo("##pickart", "pick from this folder")) {
        int offered = 0;

        // FRAMES ARE OFFERED FOR AN IMAGE TILE AND NOT FOR A SHEET, which is
        // the whole of Stage 5 in one combo box. A five-band sheet is a whole
        // file and one frame of a spritesheet is not one; but a picture drawn
        // whole in a cell is exactly what a frame is, so for that kind the
        // frames belong here. Parts carry their sheet's path in sheetPath.
        for (const Tile& tile : m_tiles) {
            const bool isPart = !tile.sheetPath.empty();
            if (isPart) {
                if (!m_newTileAsImage || tile.isAnimation || !tile.trouble.empty()) {
                    continue;   // an animation is not one picture
                }
            } else if (!tile.IsFile() || tile.IsDirectory() ||
                       tile.kind != AssetKind::Texture) {
                continue;
            }
            ++offered;
            if (ImGui::Selectable(isPart ? tile.path.c_str() : tile.label.c_str())) {
                std::snprintf(m_newTileArt, sizeof(m_newTileArt), "%s",
                              tile.path.c_str());
            }
        }
        if (offered == 0) {
            ImGui::TextDisabled(m_newTileAsImage
                                    ? "no images or frames in this folder"
                                    : "no images in this folder");
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Tile name");
    ImGui::SetNextItemWidth(420.0f);
    const bool submitted =
        ImGui::InputText("##tilename", m_newTileName, sizeof(m_newTileName),
                         ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::Checkbox("Solid - things collide with it", &m_newTileSolid);
    ImGui::TextDisabled("needs a TilemapColliderComponent on the map as well");

    ImGui::Separator();

    // ---- what the engine makes of this, live -------------------------------
    std::string    artError;
    eng::TileAsset probe;
    bool           artOk = false;
    if (m_newTileArt[0] != '\0') {
        eng::Json node = eng::Json::object();
        node[m_newTileAsImage ? "image" : "sheet"] = m_newTileArt;
        artOk = probe.Deserialize(node, artError);
    } else {
        artError = m_newTileAsImage ? "name the picture this tile draws"
                                    : "name the sheet this tile draws from";
    }

    if (artOk) {
        ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.60f, 1.0f),
                           "%dpx tiles, %d frame(s)", probe.TileSize(),
                           probe.FrameCount());

        if (const eng::TextureRef& art = probe.ArtTexture(); art && art->native) {
            const float width = 56.0f;
            const float frame = static_cast<float>(probe.TileSize()) /
                                static_cast<float>(art->width);

            if (probe.IsAutoTile()) {
                // The five bands, so the sheet can be checked at a glance
                // before the file is written. A sheet whose bands are in the
                // wrong order is the one mistake nothing else catches.
                ImGui::Image(reinterpret_cast<ImTextureID>(art->native),
                             ImVec2(width, width * static_cast<float>(eng::kSheetBands)),
                             ImVec2(0.0f, 0.0f), ImVec2(frame, 1.0f));
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextDisabled("outer corner");
                ImGui::TextDisabled("vertical edge");
                ImGui::TextDisabled("horizontal edge");
                ImGui::TextDisabled("inner corner");
                ImGui::TextDisabled("fill");
                ImGui::EndGroup();
            } else {
                // ONE SQUARE, because that is the whole of what an image tile
                // is. There are no bands to label and no shapes to check.
                ImGui::Image(reinterpret_cast<ImTextureID>(art->native),
                             ImVec2(width, width), ImVec2(0.0f, 0.0f),
                             ImVec2(frame, 1.0f));
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextDisabled("drawn whole, in every cell");
                if (probe.FrameCount() > 1) {
                    ImGui::TextDisabled("%d frames sideways - set Frames in",
                                        probe.FrameCount());
                    ImGui::TextDisabled("the Tile Palette to use them");
                }
                ImGui::EndGroup();
            }
        }
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", artError.c_str());
        ImGui::TextWrapped(
            m_newTileAsImage
                ? "An image tile is one SQUARE picture, or a sideways strip of them."
                : "A sheet is tileSize wide (times the number of frames) and "
                  "tileSize x 5 tall, and tileSize must be even. An ordinary "
                  "picture is not a sheet - switch the kind above to Image.");
    }

    const bool nameOk = (m_newTileName[0] != '\0');
    const std::string path =
        JoinPath(m_directory, std::string(m_newTileName) + ".otile");
    if (nameOk) {
        ImGui::TextDisabled("writes assets/%s", path.c_str());
    }

    ImGui::Separator();
    ImGui::BeginDisabled(!artOk || !nameOk);
    if ((submitted && artOk && nameOk) || ImGui::Button("Create", ImVec2(120, 0))) {
        std::string createError;
        if (CreateTile(m_newTileName, m_newTileArt, m_newTileSolid, m_newTileAsImage,
                       createError)) {
            std::snprintf(m_status, sizeof(m_status), "created %s", path.c_str());
            m_statusIsError = false;
            Refresh();
            ImGui::CloseCurrentPopup();
        } else {
            std::snprintf(m_status, sizeof(m_status), "%s", createError.c_str());
            m_statusIsError = true;
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "{}", createError);
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void AssetBrowserPanel::DrawNewFolderPopup() {
    if (m_openNewFolder) {
        ImGui::OpenPopup("New Folder");
        m_openNewFolder = false;
    }

    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("New Folder", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("Folder name");
    ImGui::SetNextItemWidth(320.0f);
    const bool submitted =
        ImGui::InputText("##foldername", m_newFolderName, sizeof(m_newFolderName),
                         ImGuiInputTextFlags_EnterReturnsTrue);

    const bool nameOk =
        m_newFolderName[0] != '\0' &&
        std::string_view(m_newFolderName).find_first_of("/\\:*?\"<>|") ==
            std::string_view::npos;

    if (!nameOk) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                           "a folder name cannot be empty or contain / \\ : * ? \" < > |");
    } else {
        ImGui::TextDisabled("inside %s", DisplayPath(m_directory).c_str());
    }

    ImGui::BeginDisabled(!nameOk);
    if (submitted || ImGui::Button("Create", ImVec2(120, 0))) {
        // Joined carefully rather than just concatenated, because the assets
        // root is the empty string and "" + "/" + name would start with a
        // slash - which means something quite different.
        const std::string path = m_directory.empty()
                                     ? std::string(m_newFolderName)
                                     : m_directory + "/" + m_newFolderName;
        std::string error;
        if (eng::FileSystem::CreateDirectory(path, error)) {
            std::snprintf(m_status, sizeof(m_status), "created %s", path.c_str());
            Refresh();
        } else {
            std::snprintf(m_status, sizeof(m_status), "%s", error.c_str());
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ===========================================================================
//  MOVING THINGS BETWEEN FOLDERS
// ===========================================================================

void AssetBrowserPanel::PlacePrefab(const std::string& virtualPath) {
    eng::Scene& scene = eng::Engine::Get().GetScene();

    std::string         error;
    const eng::EntityId placed = scene.InstantiatePrefab(virtualPath, error);
    eng::Entity*        instance = scene.Get(placed);
    if (instance == nullptr) {
        std::snprintf(m_status, sizeof(m_status), "%s", error.c_str());
        m_statusIsError = true;
        ENGINE_LOG_WARN(eng::Channels::kEditor, "could not place '{}': {}", virtualPath,
                        error);
        return;
    }

    // WHERE THE SCENE VIEW IS LOOKING, not the world origin - the same rule
    // ObjectMenu::CreateObject follows. A prefab placed somewhere off in the
    // distance reads as a prefab that did not work.
    instance->Transform().SetWorldPosition(eng::Engine::Get().GetCamera().Position());

    EditorState::Get().SelectOnly(placed);
    EditorState::Get().MarkDirty();

    std::snprintf(m_status, sizeof(m_status), "placed %s", instance->Name().c_str());
    m_statusIsError = false;
}

void AssetBrowserPanel::AcceptEntityDropAsPrefab(const std::string& targetDirectory) {
    // ======================================================================
    //  INSURANCE, AND SAID TO BE.
    //
    //  No part tile opens a drop target, so this is unreachable today. It is
    //  here because this is the one route whose TARGET is a directory that
    //  reaches a disk write rather than a refusal: PrefabPathFor joins the
    //  directory unsanitised, and WriteTextFile calls create_directories -
    //  so a real folder named "textures/hero.png#f0" would appear on disk
    //  and be reported as a success. ImGui keeps the SMALLEST accepting
    //  target, so any target added to a tile this size would win this one.
    // ======================================================================
    //  AND IT ASKS THE DISK, for the reason MoveLooksLegal does: a real folder
    //  called "art.png#old" parses as a frame, and refusing it on the name
    //  alone made the ordinary gesture do nothing in that folder for ever,
    //  with nothing said. If a directory really is there, prefabs go in it.
    if (eng::IsSubAsset(targetDirectory) &&
        !eng::FileSystem::Exists(targetDirectory)) {
        m_statusIsError = true;
        std::snprintf(m_status, sizeof(m_status),
                      "'%s' is one frame of an image, not a folder - a prefab "
                      "cannot be made there", targetDirectory.c_str());
        ENGINE_LOG_ERROR(eng::Channels::kEditor,
                         "refused to make a prefab in '{}', which is not a folder",
                         targetDirectory);
        return;
    }

    const ImGuiPayload* peek = ImGui::GetDragDropPayload();
    if (peek == nullptr || !peek->IsDataType(kPayloadEntity)) {
        return;
    }

    // THE TYPE IS CHECKED BEFORE THE DATA IS READ. An entity payload is an
    // EntityId - raw bytes, no terminating zero - and the asset payloads are
    // C strings; reading one as the other walks off the end. Same note as in
    // AcceptMoveInto.
    if (ImGui::AcceptDragDropPayload(kPayloadEntity) == nullptr) {
        return;
    }
    if (peek->DataSize != static_cast<int>(sizeof(eng::EntityId))) {
        return;
    }

    eng::EntityId dragged{};
    std::memcpy(&dragged, peek->Data, sizeof(dragged));

    eng::Scene&  scene  = eng::Engine::Get().GetScene();
    eng::Entity* entity = scene.Get(dragged);
    if (entity == nullptr) {
        return;   // destroyed between the drag starting and the drop
    }

    // ALREADY AN INSTANCE? Then making a prefab of it would FLATTEN it - the
    // new asset would hold plain copies with no link back to the first prefab,
    // because nested prefabs are not supported. Refused and said, rather than
    // done quietly; see the list of what is not supported in Prefab.h.
    if (scene.PrefabInstanceOf(dragged) != nullptr) {
        std::snprintf(m_status, sizeof(m_status),
                      "'%s' is already a prefab instance - unpack it first, or use "
                      "Apply to change the prefab it came from",
                      entity->Name().c_str());
        m_statusIsError = true;
        return;
    }

    PrefabRequest& request = EditorState::Get().prefabRequest;
    request.action         = PrefabAction::Create;
    request.member         = dragged;
    request.path           = PrefabPathFor(*entity, targetDirectory);
}

void AssetBrowserPanel::RevealRequestedAsset() {
    std::string& wanted = EditorState::Get().revealAsset;
    if (wanted.empty()) {
        return;
    }
    const std::string path = wanted;
    wanted.clear();   // taken and cleared first, so a failure cannot loop

    // The folder it is in: everything before the last slash, or the assets
    // root - which is the EMPTY virtual path. See the note in Navigate.
    std::string folder;
    if (const std::size_t slash = path.find_last_of('/');
        slash != std::string::npos) {
        folder = path.substr(0, slash);
    }

    // ---- a frame has no tile until its sheet is open ----------------------
    //
    // DONE BEFORE THE NAVIGATE, because Navigate refreshes and the refresh is
    // what builds the parts. Revealing a frame into a shut sheet would select
    // it correctly and highlight nothing, which is the one outcome a "show me
    // where this is" gesture must not produce.
    if (eng::IsSubAsset(path)) {
        const std::string sheet = eng::SheetPathOf(path);
        if (!IsExpanded(sheet)) {
            m_expandedSheets.push_back(sheet);
        }
    }

    if (folder != m_directory) {
        Navigate(folder);
    } else {
        // The listing has to be rebuilt even when the folder is already the
        // one being shown, because the sheet may have just been opened above.
        Refresh();
    }

    // IT MAY NAME A FRAME AS WELL AS A FILE. It used to be only a file - the
    // Inspector's "Select Asset" button - and the comment here said so; the
    // Inspector now also writes selectedAsset for a frame picked in the
    // slicer. Both are set either way, because the Inspector has something to
    // say about a frame; see the note on m_picked in the header.
    m_picked                         = path;
    EditorState::Get().selectedAsset = path;

    // BRINGING THIS PANEL TO THE FRONT IS NOT DONE HERE, and it used to be.
    // ImGui does not call Draw for a background tab, so a reveal aimed at a
    // panel sitting behind the Console never reached this function at all -
    // which is the one case the gesture exists for. EditorApp opens and
    // focuses it instead, from the frame loop, and by the time this runs the
    // panel is already in front. See RevealAssetsPanelIfRequested.
}

void AssetBrowserPanel::AcceptMoveInto(const std::string& targetDirectory) {
    if (!ImGui::BeginDragDropTarget()) {
        return;
    }

    // An ENTITY dropped here is not a move - it is a new prefab. Offered
    // first, and it takes the payload, so the loop below never sees it.
    AcceptEntityDropAsPrefab(targetDirectory);

    // LOOKED AT BEFORE IT IS ACCEPTED. GetDragDropPayload reports what is
    // being dragged without claiming it, so a target that could not carry the
    // move out declines to light up at all - rather than highlighting and
    // then refusing when the button comes up, which reads as a broken
    // feature. Same reasoning as HierarchyPanel::DrawEntityDropTarget.
    const ImGuiPayload* peek = ImGui::GetDragDropPayload();
    for (const char* id : kAllAssetPayloads) {
        if (peek == nullptr || !peek->IsDataType(id)) {
            continue;
        }

        // THE TYPE IS CHECKED BEFORE THE DATA IS READ, and that order is not
        // incidental. An ENTITY dragged out of the Hierarchy and over this
        // panel carries an EntityId - raw bytes with no terminating zero -
        // and reading those as a C string would walk off the end of the
        // payload looking for one.
        const std::string from(static_cast<const char*>(peek->Data));
        if (MoveLooksLegal(from, targetDirectory) &&
            ImGui::AcceptDragDropPayload(id) != nullptr) {
            RequestMove(from, targetDirectory);
        }
        break;
    }

    ImGui::EndDragDropTarget();
}

void AssetBrowserPanel::ApplyPendingNavigate() {
    if (m_pendingNavigate.empty()) {
        return;
    }

    // TAKEN AND CLEARED BEFORE THE WORK, so that a Refresh which somehow
    // asked to navigate again could not loop here for ever.
    const std::string going = m_pendingNavigate;
    m_pendingNavigate.clear();
    Navigate(going);
}

void AssetBrowserPanel::ApplyPendingExpand() {
    if (m_pendingExpand.empty()) {
        return;
    }
    const std::string sheet = m_pendingExpand;
    m_pendingExpand.clear();

    const auto found =
        std::find(m_expandedSheets.begin(), m_expandedSheets.end(), sheet);
    if (found == m_expandedSheets.end()) {
        m_expandedSheets.push_back(sheet);
    } else {
        m_expandedSheets.erase(found);
    }

    // The parts are built in Refresh, so the toggle IS a refresh.
    Refresh();
}

void AssetBrowserPanel::RequestMove(const std::string& from,
                                    const std::string& toDirectory) {
    eng::PrefabLibrary::Forget(from);
    m_pendingMove.from        = from;
    m_pendingMove.toDirectory = toDirectory;
    m_pendingMove.requested   = true;
}

void AssetBrowserPanel::ApplyPendingMove() {
    if (!m_pendingMove.requested) {
        return;
    }

    // Taken and CLEARED FIRST, so that a failure below cannot leave a request
    // standing that tries again on every frame for the rest of the session -
    // which would mean one impossible drag producing the same error message
    // sixty times a second.
    const PendingMove request = m_pendingMove;
    m_pendingMove             = {};

    std::string error;
    if (!eng::FileSystem::Move(request.from, request.toDirectory, error)) {
        m_statusIsError = true;
        std::snprintf(m_status, sizeof(m_status), "%s", error.c_str());
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not move '{}': {}", request.from,
                         error);
        return;
    }

    m_statusIsError = false;
    std::snprintf(m_status, sizeof(m_status), "moved %s to %s", request.from.c_str(),
                  DisplayPath(request.toDirectory).c_str());
    ENGINE_LOG_INFO(eng::Channels::kEditor, "moved '{}' into '{}'", request.from,
                    DisplayPath(request.toDirectory));

    // Into the new folder with it. LeafOf and JoinPath rather than
    // concatenation, for the empty-root reason spelled out in the rename
    // above. A folder dropped on a folder brings its own sidecars and finds
    // nothing here.
    eng::ImportLibrary::FollowAsset(
        request.from, JoinPath(request.toDirectory, LeafOf(request.from)));

    // What was selected is at a different path now. The selection is dropped
    // rather than left naming somewhere nothing lives, because the Delete key
    // acts on it - and a Delete aimed at a path that no longer exists is at
    // best confusing and at worst aimed at whatever has since taken the name.
    if (m_picked == request.from) {
        m_picked.clear();
    }
    if (EditorState::Get().selectedAsset == request.from) {
        EditorState::Get().selectedAsset.clear();
    }

    Refresh();
}

// ===========================================================================
//  IMPORTING FILES DRAGGED IN FROM OUTSIDE
// ===========================================================================

bool AssetBrowserPanel::ContainsPoint(float screenX, float screenY) const {
    // Asked of the window AS IT IS RIGHT NOW rather than remembered from the
    // last frame. Begin() has already run by the time Draw is called, so the
    // position and size are current even though nothing has been drawn into
    // the window yet.
    const ImVec2 min  = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    return screenX >= min.x && screenY >= min.y && screenX <= min.x + size.x &&
           screenY <= min.y + size.y;
}

std::string AssetBrowserPanel::FolderUnderPoint(float screenX, float screenY) const {
    for (const Row& row : m_rows) {
        if (!row.isDirectory) {
            continue;
        }
        if (screenX >= row.minX && screenX <= row.maxX && screenY >= row.minY &&
            screenY <= row.maxY) {
            return row.virtualPath;
        }
    }
    // Over no folder in particular, which is the ordinary case: it lands in
    // whatever folder is being browsed, which is the one you are looking at.
    return m_directory;
}

void AssetBrowserPanel::ImportDroppedFiles() {
    const eng::EventPump& events = eng::Engine::Get().Events();
    if (events.DroppedFiles().empty()) {
        return;
    }

    std::size_t imported = 0;
    std::size_t refused  = 0;
    std::string lastLanded;
    std::string lastError;
    std::string lastDestination;

    for (const eng::DroppedFile& file : events.DroppedFiles()) {
        // NOT OURS - and this is the entire reason a drop carries a position.
        // One window holds every panel, so a file let go of over the Scene
        // view is reported to this code as well and has to be left alone.
        if (!ContainsPoint(file.x, file.y)) {
            continue;
        }

        // ---- a .oimport on its own is refused -----------------------------
        //
        // IT WOULD OVERWRITE SETTINGS NOBODY ASKED ABOUT. FileSystem::Import
        // makes a colliding name unique from its stem and its extension, and a
        // stem is everything before the LAST dot - so "player.png.oimport"
        // dropped into a folder that already holds "player.png" is not a
        // collision at all. It lands as precisely the sidecar that image
        // reads, replacing whatever was there.
        //
        // These are derived files. Dropping one on its own is never the thing
        // somebody meant, and that image's own settings are one click away in
        // the Inspector. A dropped FOLDER is unaffected and brings its
        // sidecars along beside the assets they belong to, which is right.
        if (eng::ImportLibrary::IsSidecar(file.path)) {
            ++refused;
            lastError = "import settings are made in the Inspector, not copied in";
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "refused to import '{}': {}",
                             file.path, lastError);
            continue;
        }

        const std::string destination = FolderUnderPoint(file.x, file.y);
        std::string       landed;
        std::string       error;
        if (eng::FileSystem::Import(file.path, destination, landed, error)) {
            ++imported;
            lastLanded      = landed;
            lastDestination = destination;
            ENGINE_LOG_INFO(eng::Channels::kEditor, "imported '{}' as '{}'", file.path,
                            landed);
        } else {
            ++refused;
            lastError = error;
            // AT ERROR rather than Warning. config/engine.json sets the
            // threshold to Error in this project, so a warning would reach
            // neither the Console nor the log file - and a refusal to import
            // something the user has just dropped is exactly the kind of
            // thing that must not disappear. See ShellOpen.h.
            ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not import '{}': {}",
                             file.path, error);
        }
    }

    if (imported == 0 && refused == 0) {
        return;   // every one of them landed on some other panel
    }

    if (refused == 0 && imported == 1) {
        m_statusIsError = false;
        std::snprintf(m_status, sizeof(m_status), "imported %s", lastLanded.c_str());
    } else if (refused == 0) {
        m_statusIsError = false;
        std::snprintf(m_status, sizeof(m_status), "imported %zu item(s) into %s",
                      imported, DisplayPath(lastDestination).c_str());
    } else if (imported == 0) {
        m_statusIsError = true;
        std::snprintf(m_status, sizeof(m_status), "%s", lastError.c_str());
    } else {
        // BOTH NUMBERS AND THE REASON. "imported 3 of 5" with no explanation
        // is a message nobody can act on.
        m_statusIsError = true;
        std::snprintf(m_status, sizeof(m_status), "imported %zu, refused %zu - %s",
                      imported, refused, lastError.c_str());
    }

    Refresh();

    // ---- a script that has just arrived is not compiled yet ----------------
    //
    // Scripts normally rebuild when the editor regains focus, which is the
    // right trigger for somebody editing one in another program. It is the
    // wrong trigger here: having dropped a .cpp in, the obvious next thing to
    // do is attach it, and finding it missing from the list reads as the
    // import having failed.
    //
    // NeedsRebuild answers exactly the right question, which is why it is
    // asked rather than testing the file extension: an imported image costs
    // nothing, and a dropped FOLDER with scripts inside it is noticed just as
    // readily as a single file would be.
    if (imported > 0 && ScriptBuild::NeedsRebuild() && !ScriptBuild::IsBuilding()) {
        // IsBuilding stands in for BeginAsync in the condition above so that
        // the Console is emptied only when a build is really about to start -
        // and before it, because the compiler's first lines arrive on a
        // worker thread the moment BeginAsync returns. See
        // ClearConsoleForCompile in EditorApp.h.
        ClearConsoleForCompile();

        if (ScriptBuild::BeginAsync()) {
            const std::string note = std::string(m_status) + " - building scripts...";
            std::snprintf(m_status, sizeof(m_status), "%s", note.c_str());
        }
    }
}

void AssetBrowserPanel::DrawImportHighlight() {
    const eng::EventPump& events = eng::Engine::Get().Events();
    if (!events.DragHovering() || !ContainsPoint(events.DragX(), events.DragY())) {
        return;
    }

    const std::string destination = FolderUnderPoint(events.DragX(), events.DragY());

    // ON THE FOREGROUND DRAW LIST, which is unusual enough to be worth saying
    // why. ImGui knows nothing at all about this drag - it started in another
    // program - so none of its own highlighting happens and there is no
    // hovered item to hang an outline off. Drawing straight onto the
    // foreground puts the feedback above the grid child window, which would
    // otherwise clip it.
    ImDrawList*  draw = ImGui::GetForegroundDrawList();
    const ImVec2 min  = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max(min.x + size.x, min.y + size.y);

    draw->AddRect(min, max, IM_COL32(120, 190, 250, 255), 4.0f, 0, 3.0f);

    char label[320];
    std::snprintf(label, sizeof(label), "drop to import into %s",
                  DisplayPath(destination).c_str());

    // Behind a filled box, because this is drawn over whatever the panel
    // happens to contain and pale text on top of a thumbnail is unreadable.
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const ImVec2 at(min.x + (size.x - textSize.x) * 0.5f, max.y - textSize.y - 10.0f);
    draw->AddRectFilled(ImVec2(at.x - 8.0f, at.y - 4.0f),
                        ImVec2(at.x + textSize.x + 8.0f, at.y + textSize.y + 4.0f),
                        IM_COL32(18, 28, 42, 235), 4.0f);
    draw->AddText(at, IM_COL32(190, 220, 255, 255), label);

    // And the folder the cursor is actually over, when it is over one. The
    // message above names it, but a name is something to read and an outline
    // is something to see.
    if (destination == m_directory) {
        return;
    }
    for (const Row& row : m_rows) {
        if (row.virtualPath != destination) {
            continue;
        }
        draw->AddRect(ImVec2(row.minX - 2.0f, row.minY - 2.0f),
                      ImVec2(row.maxX + 2.0f, row.maxY + 2.0f),
                      IM_COL32(120, 190, 250, 255), 3.0f, 0, 2.0f);
        break;
    }
}

void AssetBrowserPanel::OnHidden() {
    // See the note in the header: positions recorded while this panel was on
    // screen would go on answering "which folder is under this point" with a
    // layout that is no longer true.
    m_rows.clear();
}

} // namespace editor
