#pragma once

// ============================================================================
//  AssetBrowserPanel.h - the Assets panel: the files on disk.
//
//  This is what Unity calls the Project window. It shows the folders and files
//  that make up the game, with a thumbnail for every image, and it is where
//  you drag things from:
//
//    * drag an image into the Scene view to place it
//    * drag an image onto an entity to give it that sprite
//    * drag a .cpp onto an entity to attach that script
//    * double-click a scene to open it
//    * "+ Script" writes a new script from the template
//
//  And two things that go the other way - INTO the panel rather than out of
//  it:
//
//    * drag files or folders in from the desktop to IMPORT them. They are
//      copied into whichever folder the cursor is over, or into the folder
//      being browsed. The original is never touched.
//    * drag a tile onto a folder, or onto any step of the path trail at the
//      top, to MOVE it there. Folders can be dragged too, and the path trail
//      is what makes moving something back OUT of a folder possible.
//    * drag an ENTITY in from the Hierarchy to make a PREFAB of it, in
//      whichever folder it lands on. That is Unity's gesture for the same
//      thing, and it is the one that lets you choose where the file goes.
//
//  ==========================================================================
//  ONE ROOT: assets/, and everything in it is yours to arrange.
//
//  Scenes, images and scripts all live in the same tree, in whatever folders
//  suit the game - enemies/, player/, ui/. There is no designated scripts
//  folder, because the editor compiles every .cpp and .h it finds anywhere
//  under assets/, so a script works wherever you decide to put it.
//
//  That is the point of the arrangement: the things that belong to one part of
//  your game stay together, rather than being split across the project by what
//  language they happen to be written in.
//
//  The compiled result does NOT go here - it is written to .build/ beside
//  assets/, so the tree this panel shows contains only files you wrote.
//
//  ==========================================================================
//  THE LISTING IS CACHED AND REFRESHED ON DEMAND, not read every frame.
//  Scanning a folder sixty times a second, for a panel whose contents only
//  change when somebody creates a file, is exactly the sort of cost a tool
//  must not impose on the thing it is meant to be helping with. It refreshes
//  when you navigate, when you create something, and when you press Refresh.
// ============================================================================

#include "AssetDragDrop.h"   // AssetKind, worked out once per row
#include "Panel.h"

#include <engine/Engine.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace editor {

class AssetBrowserPanel final : public Panel {
public:
    AssetBrowserPanel();

    const char* Title() const override { return "Assets"; }
    void        Draw() override;

    // The recorded tile positions are thrown away when the panel stops being
    // visible. They are used to work out which folder a file dragged in from
    // the desktop landed on, and a layout from the last time this panel was
    // on screen - possibly a different folder, possibly a different window
    // size - would answer that question confidently and wrongly.
    //
    // WHICH SHEETS ARE OPEN IS DELIBERATELY *NOT* DISCARDED HERE, and the
    // analogy with the positions is the wrong one to follow. A position goes
    // stale because the thing it describes has moved; "this sheet is open"
    // is a decision somebody made, and it is still true when the panel comes
    // back. Clearing it here would shut every sheet whenever the Assets tab
    // lost visibility.
    void OnHidden() override;

private:
    // ======================================================================
    //  ONE ROW OF THE GRID - which is NOT the same thing as one file.
    //
    //  A sliced spritesheet shows its frames underneath it, and a frame is not
    //  a file: "textures/hero.png#f0" names a rectangle of an image that the
    //  engine cuts out when something asks for it. There is nothing on disk to
    //  rename, delete, open in an image viewer or move to another folder.
    //
    //  ==================================================================
    //  `file` IS ENGAGED IF AND ONLY IF THIS ROW IS A THING ON DISK.
    //
    //  That is the whole point of this type. "Is this a real file?" used to be
    //  a question every gesture in this panel would have had to remember to
    //  ask - and there are eighteen of them, counting the two dialogs, both
    //  keys, the context menu, the drag source, the move targets and the
    //  import highlight. One of them forgetting is a modal that says "Delete
    //  f0?" and removes hero.png.
    //
    //  Now the DirEntry travels on the row that draws it, so the dangerous row
    //  - one whose name says "f0" and whose path says "textures/hero.png" -
    //  cannot be built. RequestRename and RequestDelete read *file, and they
    //  read it only after IsFile().
    //
    //  REJECTED: a synthetic DirEntry per frame, with isDirectory false and a
    //  made-up byteSize. It would have cost no new type and would have been
    //  accepted by every gesture in the panel, which is precisely the problem:
    //  the thumbnail loop would have cut a texture per frame on the UI thread,
    //  and the Delete key would have put the one modal in this editor that
    //  destroys data in front of something that is not a file.
    //
    //  REJECTED: keeping m_entries and asking eng::IsSubAsset at each gate.
    //  Eighteen places that have to remember, defended by nothing but review.
    //
    //  ==================================================================
    //  AND THE PICTURE TRAVELS WITH IT TOO.
    //
    //  There used to be a second vector of textures indexed in step with the
    //  entries, with a comment warning that filtering one without the other
    //  would slide every image one place along from the file it belongs to.
    //  That lockstep is gone: a row carries its own image, and a frame's row
    //  carries its SHEET's image plus the uv rectangle to cut out of it, which
    //  is what makes expanding a 96-frame sheet cost no textures at all.
    // ======================================================================
    struct Tile {
        std::string path;    // the virtual path, or "<sheet>#<name>" for a part
        std::string label;   // what is written under the icon
        AssetKind   kind = AssetKind::Unknown;

        // Engaged for a file or a folder; never for a part. See above.
        std::optional<eng::FileSystem::DirEntry> file;

        bool IsFile() const { return file.has_value(); }
        bool IsDirectory() const { return file.has_value() && file->isDirectory; }

        // ---- a part of a sheet --------------------------------------------
        std::string sheetPath;             // the image it was cut from
        bool        isAnimation = false;
        float       fps         = 0.0f;
        bool        loop        = false;
        std::size_t frameCount  = 0;       // for an animation
        std::string firstFrame;            // the frame an animation draws as

        // Why this part cannot be drawn, in words for a person. Empty when it
        // can. Worked out in AddSheetParts, where the sheet's size is known.
        std::string trouble;

        // ---- the picture ---------------------------------------------------
        //
        // For a file: its own thumbnail. For a part: THE SHEET'S texture, with
        // the uv rectangle below naming the part of it to draw. Holding the
        // reference here is what keeps the image loaded; letting go of a row
        // unloads it if nothing else wants it. See render/Texture.h.
        eng::TextureRef texture;

        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
        int   pixelW = 0, pixelH = 0;      // the part's size in sheet pixels

        // ---- the lid -------------------------------------------------------
        bool expandable = false;           // a sliced sheet, so it has an arrow
        bool expanded   = false;

        // What is inside, known even while it is SHUT - so the arrow's
        // tooltip can say what opening it would show. `parts` is only filled
        // in when it is open, because the whole point is that a closed sheet
        // costs nothing.
        std::size_t sheetFrames     = 0;
        std::size_t sheetAnimations = 0;

        // std::vector is one of the three containers the standard allows an
        // INCOMPLETE element type, which is what makes this self-reference
        // legal rather than merely something the compiler happened to accept.
        std::vector<Tile> parts;
    };


    void Navigate(const std::string& virtualDirectory);
    void Refresh();

    void DrawBreadcrumb();

    // The folder path as a row of buttons - assets / textures / enemies -
    // every one of which can be clicked to go there AND dropped onto to move
    // something there.
    void DrawPathTrail();

    void DrawEntry(const Tile& tile, int index);

    // Puts an instance of a prefab in the middle of the Scene view. Used by
    // double-click and by the context menu, which must agree.
    void PlacePrefab(const std::string& virtualPath);

    // Takes an ENTITY dropped on this panel and asks for a prefab to be made
    // of it in `targetDirectory`.
    //
    // THE REQUEST IS RECORDED, NOT CARRIED OUT. Making a prefab destroys the
    // entity and replaces it with an instance, and this panel is drawing while
    // the Hierarchy is still holding that entity. See PrefabRequest in
    // EditorApp.h.
    void AcceptEntityDropAsPrefab(const std::string& targetDirectory);

    // Navigates to and highlights whatever EditorState::revealAsset names,
    // which is how the Inspector's "Select Asset" button reaches this panel.
    void RevealRequestedAsset();

    void DrawNewScriptPopup();
    void DrawNewTilePopup();
    void DrawNewFolderPopup();

    // Right-click: Open, Show in File Explorer, Rename, Delete.
    void DrawEntryContextMenu(const Tile& tile);

    // Both of these are MODAL and both are opened from Draw rather than from
    // inside a tile's id scope - a popup opened while a PushID is active
    // belongs to that id and never appears. Same reason the New Script popup
    // works this way.
    void DrawRenamePopup();
    void DrawDeletePopup();

    // ======================================================================
    //  Records which tile was just clicked, and tells the Inspector about it.
    //
    //  ONE DOOR, because there are two ways to pick a tile - a left click and
    //  a right click opening the context menu - and they have to agree about
    //  what picking means or the menu acts on a different file from the one
    //  with the border round it.
    // ======================================================================
    void Pick(const Tile& tile);

    // The same decision from a path and a flag, for a part - which has no
    // DirEntry to hand. Both of the writes live here, so there is still one
    // place that decides what picking means.
    void Pick(const std::string& virtualPath, bool isDirectory,
              bool isPart = false);

    // Records what to act on and raises the flag Draw watches. Called from
    // the context menu and from the Delete key.
    // ======================================================================
    //  BOTH TAKE A Tile AND BOTH REFUSE ONE WITH NO DirEntry, and that is
    //  load-bearing rather than tidy.
    //
    //  These are the two doors to the only dialogs in this editor that
    //  destroy something. Four gestures reach them today - the two keys and
    //  two context-menu items - and putting the refusal INSIDE them rather
    //  than at each gesture means a fifth gesture added later passes through
    //  it whether its author thought about frames or not.
    // ======================================================================
    void RequestRename(const Tile& tile);
    void RequestDelete(const Tile& tile);

    // True when this row is a thing on disk; otherwise says why not, in
    // m_status, and the caller returns. The ONE wording of that refusal.
    bool RefuseIfNotAFile(const Tile& tile);

    // Fills in a sheet tile's arrow and, when it is open, its parts. Called
    // once per image per Refresh - never per frame, and never per part.
    void AddSheetParts(Tile& sheet);

    // One frame or one animation of a sheet, drawn to look and behave like
    // any other image - which is the whole of Stage 3 in one sentence.
    void DrawPartTile(const Tile& part);
    void DrawPartContextMenu(const Tile& part);

    // Hands a file to the operating system, reporting either way. See
    // ShellOpen.h for why the reporting matters more than it sounds.
    void OpenInSystemEditor(const std::string& virtualPath);
    void RevealAsset(const std::string& virtualPath);

    // ======================================================================
    //  MOVING THINGS BETWEEN FOLDERS - drag a tile onto a folder, or onto any
    //  step of the path trail above the grid.
    //
    //  THE MOVE IS DEFERRED to the top of the next Draw, for exactly the
    //  reason the engine defers destroying an entity. It is discovered half
    //  way through the loop that is drawing m_entries, and carrying it out on
    //  the spot would rebuild that list while the loop still held a reference
    //  into it. The editor does not get an exemption from the engine's own
    //  rules - see EditorState::requestedScene for the same argument about
    //  loading a scene.
    // ======================================================================
    struct PendingMove {
        std::string from;
        std::string toDirectory;
        bool        requested = false;
    };
    PendingMove m_pendingMove;

    void RequestMove(const std::string& from, const std::string& toDirectory);
    void ApplyPendingMove();

    // ======================================================================
    //  AND NAVIGATION IS DEFERRED FOR THE SAME REASON, which it was not.
    //
    //  Double-clicking a folder used to call Navigate on the spot, from inside
    //  the loop drawing the rows - and Navigate refreshes, which throws that
    //  very list away. It survived on a technicality: Navigate copied the
    //  path into m_directory one line before Refresh invalidated the reference
    //  it had been handed, and the loop then re-read the REBUILT list's size
    //  and carried on drawing the new folder's files at the old folder's
    //  indices for the rest of the frame.
    //
    //  A row is now held as a reference for the whole of DrawEntry, so that
    //  technicality became a dangling read. Deferring it costs one frame that
    //  nobody can see.
    // ======================================================================
    std::string m_pendingNavigate;
    void        ApplyPendingNavigate();

    // And opening or shutting a sheet, which refreshes for the same reason.
    std::string m_pendingExpand;
    void        ApplyPendingExpand();

    // Opens a drop target for the item that was just drawn, and moves
    // whatever is let go over it into `targetDirectory`. Every folder tile
    // and every step of the path trail calls this one function, so the two
    // cannot end up disagreeing about what they accept.
    void AcceptMoveInto(const std::string& targetDirectory);

    // ======================================================================
    //  IMPORTING - files dragged in from the desktop or a file explorer.
    //
    //  Nothing about this goes through ImGui. ImGui only knows about drags
    //  that start inside the program; one that starts in Explorer is reported
    //  by the operating system as a position and a list of files, and that is
    //  all there is to work with. So this panel does its own hit testing
    //  against its own window, and draws its own highlight.
    // ======================================================================
    void ImportDroppedFiles();
    void DrawImportHighlight();

    // Is this point, in screen pixels, inside the Assets window? Asked of
    // every dropped file, because one window holds every panel and a file let
    // go of over the Scene view arrives here too.
    bool ContainsPoint(float screenX, float screenY) const;

    // Which folder a point on screen is over, falling back to the folder
    // being browsed when it is over no folder in particular.
    std::string FolderUnderPoint(float screenX, float screenY) const;

    // Where each tile ended up the last time the grid was drawn.
    //
    // Kept because a file dragged in from outside arrives with a POSITION and
    // nothing else: ImGui never saw that drag, so it cannot be asked what was
    // underneath it. Plain floats rather than ImVec2 so this header does not
    // have to include ImGui for four numbers.
    //
    // ONLY A PLACE A FILE COULD BE WRITTEN, which narrows what belongs in
    // here. Its two readers both answer "which FOLDER is this point over?",
    // so a row for a frame of a spritesheet would make FolderUnderPoint hand
    // back "textures/hero.png", DrawImportHighlight promise an import into
    // it, and FileSystem::Import refuse on arrival because it is not a
    // directory. A part tile records nothing here, on purpose.
    struct Row {
        std::string virtualPath;
        bool        isDirectory = false;
        float       minX = 0.0f;
        float       minY = 0.0f;
        float       maxX = 0.0f;
        float       maxY = 0.0f;
    };
    std::vector<Row> m_rows;

    // ======================================================================
    //  WHAT IS SELECTED LIVES IN EditorState, not here.
    //
    //  It was a member of this panel - m_selectedPath - for as long as this
    //  panel was the only thing that cared. The Inspector now shows the
    //  selected file's import settings, so two panels need the same answer,
    //  and a tile's border disagreeing with the panel on the right about which
    //  file is being discussed is the bug that arrangement invites.
    //
    //  A virtual path rather than an index, which has not changed: a refresh
    //  reorders the list and an index would then name a different file.
    //
    //  See EditorState::selectedAsset in EditorApp.h for which gestures clear
    //  it, and note that the stale-selection drop now happens there too -
    //  PruneSelection runs every frame whatever has focus, and this panel
    //  could only ever do it on the frames it was focused.
    //
    //  ======================================================================
    //  BUT A FOLDER IS NOT AN ASSET, AND THAT IS WHY THERE ARE STILL TWO.
    //
    //  m_picked below is THIS PANEL'S idea of "the tile with the border, and
    //  the thing Delete and F2 act on". That can be a folder. The Inspector's
    //  idea is "the asset being described", and a folder is not one of those -
    //  publishing one would replace the entity Inspector with the words "this
    //  kind of file has no import settings" about a directory, and leave it
    //  there, because nothing clears it on navigation any more.
    //
    //  So m_picked is the panel's own, and EditorState::selectedAsset is
    //  DERIVED from it in Pick() - set for a file, cleared for a folder.
    //
    //  ======================================================================
    //  AND THERE IS A THIRD KIND OF THING NOW: a frame of a spritesheet.
    //
    //  Neither a folder nor a file. selectedAsset IS set for one, because the
    //  Inspector has something to say about it - which rectangle of which
    //  image it is - and DrawAssetInspector tests for a sub-asset before it
    //  asks about import settings at all.
    //
    //  NOTHING MAY RESOLVE m_picked TO ITS SHEET IN ORDER TO ACT ON IT. The
    //  "+ Tile" prefill resolves to the sheet, and it is allowed to because it
    //  only READS a path to put in a text box. The tempting one-line "fix" for
    //  the Delete key that does nothing on a picked frame - look the row up by
    //  eng::SheetPathOf - would find the SHEET's row and hand it to
    //  RequestDelete, which is a confirmation dialog saying "f0" and an image
    //  file leaving the disk.
    //
    //  ======================================================================
    //  "ONE WRITE SITE" WAS TRUE AND IS NOT ANY MORE.
    //
    //  That claim stood here for as long as this panel was the only thing that
    //  picked anything. The Inspector now writes selectedAsset in three
    //  places, all of them about frames: the Frames list's "Look at it", a
    //  frame clicked in the slicer's preview, and "Open the sheet" on a frame.
    //  So clicking a frame in the slicer moves the Inspector while this
    //  panel's border stays where it was - the exact disagreement the sentence
    //  was written to prevent, now reachable.
    //
    //  Left as a known limit rather than papered over: the two ideas are
    //  genuinely different - m_picked also answers for folders - and a
    //  mechanism for one panel to push a border into another is a bigger
    //  change than the disagreement costs. The Inspector's "Look at it" is the
    //  gesture that would want it.
    // ======================================================================
    std::string m_picked;

    // ======================================================================
    //  WAS THE PICKED THING A PART, RATHER THAN A ROW THAT HAPPENS TO MATCH?
    //
    //  THE PATH IS NOT AN IDENTITY, and assuming it was is the one genuinely
    //  dangerous bug this feature nearly shipped. Two rows can carry the same
    //  string: a frame called f0 of hero.png is "textures/hero.png#f0", and a
    //  REAL FILE of that name can sit beside it - '#' is a legal filename
    //  character, and this panel's own F2 dialog will produce one.
    //
    //  The Delete and F2 lookup searched m_tiles for m_picked, so clicking the
    //  FRAME and pressing Delete found the real file's top-level row, passed
    //  the "is this on disk?" test with that row's DirEntry, and opened the
    //  confirmation dialog naming the frame's own path - then deleted an image
    //  file the user had never selected. There is no undo for the disk.
    //
    //  Nesting the parts inside their sheet's row was supposed to be the gate.
    //  It is not: the nesting hides a part from the SEARCH, and the search
    //  still found something else with the same name.
    //
    //  So what was picked is recorded, not just where. Written in Pick, which
    //  is the one door, so it cannot drift from m_picked.
    // ======================================================================
    bool m_pickedIsPart = false;

    // Writes <the folder being browsed>/<name>.cpp from the template, then
    // compiles and loads it so it can be attached immediately. Returns false
    // with a reason - it refuses to overwrite, because silently replacing
    // somebody's script with a blank template would be unforgivable.
    bool CreateScript(const std::string& name, std::string& outError);

    // ======================================================================
    //  IS THIS SCRIPT NAME ALREADY TAKEN? The file that has it, or nullptr.
    //
    //  TWO FILES CANNOT BOTH DEFINE ONE CLASS. They used to be able to: every
    //  script sat in an anonymous namespace, which made the class internal to
    //  its own file and two `class Player` definitions merely confusing.
    //  Scripts share the global namespace now, so that they can refer to one
    //  another, and a duplicate is a real one-definition-rule violation - in
    //  the editor only the first registers and the other silently never runs,
    //  and in an export every script is linked into ONE library, where the
    //  linker folds the duplicate away without a word and can pair one
    //  class's constructor with the other's layout.
    //
    //  So it is refused at the two moments a name is chosen rather than
    //  reported afterwards.
    //
    //  `ignorePath` is the file being acted on, so that renaming a file to the
    //  name it already has is not a clash with itself.
    // ======================================================================
    const std::string* ScriptNameTaken(const std::string& name,
                                       const std::string& ignorePath) const;

    // Writes <the folder being browsed>/<name>.otile naming `art`. Refuses to
    // overwrite, like CreateScript, and for the same reason.
    //
    // `asImage` picks WHICH KEY the file uses - "image" for one picture drawn
    // whole, "sheet" for a five-band auto-tile - because in a .otile the key
    // is the kind. See engine/include/engine/tilemap/TileAsset.h.
    bool CreateTile(const std::string& name, const std::string& art, bool solid,
                    bool asImage, std::string& outError);

    // The assets root is the EMPTY virtual path - see the note in the .cpp.
    std::string                            m_directory;
    std::vector<Tile> m_tiles;

    // ======================================================================
    //  THE ROWS A REFRESH REPLACED, KEPT ALIVE UNTIL THE NEXT FRAME.
    //
    //  A row owns a TextureRef, and that reference is the last one for any
    //  image nothing else in the editor happens to be using. Refresh can
    //  happen FROM INSIDE Draw - the delete dialog's button calls it, and so
    //  does rename - and by that point this frame's draw list already holds
    //  that texture's raw SDL handle, submitted by the tile that drew it.
    //  Letting the old rows die there destroys the texture while the renderer
    //  still intends to draw with it: SDL_RenderGeometry against freed
    //  memory, at the end of the same frame.
    //
    //  So the retired rows are parked here and dropped at the TOP of the next
    //  Draw, beside the other deferrals, by which time the draw list that
    //  mentioned them has been through the renderer and thrown away.
    //
    //  This is the same deferral this panel already makes for a move, a
    //  navigation and an expansion, and for the same reason: something
    //  discovered mid-frame must not be acted on mid-sentence.
    // ======================================================================
    std::vector<Tile> m_retired;

    // ======================================================================
    //  Which sheets are open. Paths rather than indices, because a refresh
    //  reorders the grid - the same reasoning as m_picked below.
    //
    //  NOTHING EVER TAKES A PATH OUT, except the arrow being clicked again,
    //  and that is a stated limit rather than an oversight. Renaming, moving
    //  or deleting a sheet leaves its old path in here: harmless, because a
    //  path that matches nothing costs one string and is never looked at, but
    //  it has two visible consequences worth knowing.
    //
    //  A renamed sheet comes back SHUT, because its new path was never in
    //  here. And a NEW file created at a path that was open earlier in the
    //  session comes up already open, with nobody having asked for it.
    //
    //  Pruning it properly means a fourth place that has to remember, beside
    //  the three that already call eng::ImportLibrary::FollowAsset - which is
    //  the duplication that function's own comment argues against. The set
    //  also grows only by the number of distinct sheets opened in a session.
    // ======================================================================
    std::vector<std::string> m_expandedSheets;
    bool IsExpanded(const std::string& sheetPath) const;
    bool                                   m_valid = false;


    char  m_newScriptName[64] = {};
    char  m_newTileName[64]   = {};
    char  m_newTileArt[256]   = {};
    bool  m_newTileSolid      = true;
    bool  m_newTileAsImage    = false;   // "image" rather than "sheet"
    bool  m_openNewTile       = false;
    char  m_newFolderName[64] = {};
    bool  m_openNewScript     = false;

    // Class name -> the file that defines it, as of the last time a dialog
    // that needs it was opened. Rebuilt there and not per frame: see
    // ScriptBuild::GatherScriptClasses.
    std::map<std::string, std::string> m_scriptClasses;
    bool  m_openNewFolder     = false;
    char  m_status[256]       = {};

    // Whether m_status is a refusal rather than a confirmation, which decides
    // how it is drawn. A failure shown in the same grey as a success is a
    // failure nobody reads - see ShellOpen.h.
    bool  m_statusIsError     = false;

    // ---- the rename dialog ----------------------------------------------
    std::string m_renamePath;            // what is being renamed
    std::string m_renameOriginal;        // its current name, for the message
    char        m_renameBuffer[128] = {};
    bool        m_openRenamePopup   = false;

    // ---- the delete dialog ------------------------------------------------
    std::string m_deletePath;
    std::string m_deleteName;
    bool        m_deleteIsDirectory = false;
    std::size_t m_deleteCount       = 0;   // how many files are inside a folder
    bool        m_openDeletePopup   = false;
    float m_iconSize          = 72.0f;

};

} // namespace editor
