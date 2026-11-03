#pragma once

// ============================================================================
//  ImportSettings.h - how a file on disk becomes the thing the game draws.
//
//  An image file does not say how it wants to be scaled. A 16x16 character
//  blown up to 64x64 can come out crisp and blocky, or soft and blurred, and
//  BOTH ARE RIGHT - for different pictures. Pixel art wants the blocky one; a
//  photograph, a gradient or a piece of interface art wants the smooth one.
//  Nothing in the .png says which, because it is not a property of the pixels.
//
//  So it is recorded beside the file, in a small JSON file of its own:
//
//      textures/player.png            the picture
//      textures/player.png.oimport    how to import it
//
//  Select the picture in the Assets panel and the Inspector shows that second
//  file, as a choice between "Pixel Art" and "Smooth / UI".
//
//  ==========================================================================
//  WHY A FILE PER ASSET RATHER THAN ONE LIST FOR THE PROJECT
//
//  Because a folder of sprites is a thing people copy. Drag `enemies/` into
//  another project and the settings come with it; with one central list they
//  would be left behind, and the sprites would arrive looking wrong with
//  nothing to explain why. It is also the one arrangement where two people
//  adding settings to two different images cannot conflict.
//
//  The cost is real and worth stating: a sidecar's path is DERIVED rather than
//  referenced, so nothing in any scene names it, and anything that walks the
//  project looking for files it needs will not find one by looking. That is
//  why the exporter has an explicit step for them - see CollectReferencedAssets
//  in editor/src/GameExport.cpp - and why FollowAsset below exists at all.
//
//  THE WHOLE ORIGINAL NAME IS KEPT, including the extension: `player.png`
//  becomes `player.png.oimport`, not `player.oimport`. In a folder holding
//  both `logo.png` and `logo.jpg` the short form would be ONE file claiming to
//  describe two different pictures, and whichever was imported second would
//  silently take the other's settings.
//
//  ==========================================================================
//  A MISSING FILE IS NOT A PROBLEM, AND THAT IS THE IMPORTANT PART.
//
//  Every default below is exactly what the engine did before any of this
//  existed. A project with no .oimport files anywhere behaves identically to
//  one built against the version of the engine that had no import settings at
//  all - same pixels, same code path, nothing to migrate and nothing to opt
//  out of.
//
//  That is deliberate and it is load-bearing. An asset pipeline that changes
//  how existing art looks the moment it is added is one that cannot be added.
//
//  So: `Nearest` for a texture, because ResourceManager used to set
//  SDL_SCALEMODE_NEAREST on every image it loaded; `Linear` for a font,
//  because the atlas baker used to pass `smooth = true`. The two differ for
//  the good reason given in each place, and neither changes here.
//
//  The other half of the same rule: Save REMOVES the sidecar when the settings
//  it is given are the defaults. "No file" and "a file saying Nearest" would
//  otherwise be two spellings of one state, and a project would accumulate one
//  of them per image for no reason anybody could see.
//
//  ==========================================================================
//  NO JSON IN THIS HEADER, ON PURPOSE.
//
//  Every other Serialize/Deserialize pair in this engine takes a `const Json&`
//  and this one deliberately has none. The reading and writing lives entirely
//  inside ImportSettings.cpp, reached through ImportLibrary.
//
//  The reason is that nlohmann/json.hpp is a very large header, and this one is
//  included by the texture loader, the font baker, the exporter and two panels
//  of the editor in order to describe a single enum. A component's Serialize
//  has no such problem because a scene file is the only way a component is ever
//  built; these settings are asked for on the DRAWING path, by code that has no
//  business knowing what format they were stored in.
//
//  ONE PLACE READS THE FILE. If that ever stops being true, this is the comment
//  that was wrong.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eng {

// The extension, SPELLED ONCE. The Assets panel hides these files, the
// exporter ships them, the importer refuses them and FollowAsset moves them;
// five pieces of code agreeing about a string literal is five chances to
// disagree.
inline constexpr const char* kImportExtension = ".oimport";

// ---------------------------------------------------------------------------
//  How an image is sampled when it is drawn at any size but its own.
//
//  This is the setting the whole file exists for. It maps directly onto SDL's
//  scale mode, and it is a property of the GRAPHICS OBJECT rather than of the
//  pixels - which is why changing it in the editor can take effect on an image
//  that is already loaded and already on screen, with nothing to recompute.
// ---------------------------------------------------------------------------
enum class TextureFilter {
    // Take the colour of the nearest pixel. Enlarging doubles pixels up, so a
    // 16x16 sprite at 4x is 64x64 of visibly square pixels. That is what pixel
    // art means and what it needs: the hard edges ARE the art, and anything
    // that averages them away has thrown the art away.
    Nearest,

    // Blend the four surrounding pixels. Enlarging gives a soft gradient
    // rather than squares, which is right for a photograph, a gradient, a soft
    // shadow, or interface art drawn at a higher resolution than it is shown
    // at - and wrong for pixel art, where it reads as a smear.
    Linear,
};

const char* TextureFilterName(TextureFilter filter);

// Reads one back. `outRecognized` is false for a name this engine does not
// have, and `fallback` is returned.
//
// IT NEVER WARNS AND NEVER FAILS, exactly like ParseTileFrameMode: the CALLER
// decides what an unknown name means, because the answer differs. Reading a
// file somebody typed by hand it is worth a message; offering a default to
// code that asked for one it is not.
//
// The fallback is a parameter rather than fixed, because the two kinds of
// asset below default to OPPOSITE values and a shared default would silently
// be wrong for one of them.
TextureFilter ParseTextureFilter(std::string_view name, bool& outRecognized,
                                 TextureFilter fallback = TextureFilter::Nearest);

// ===========================================================================
//  SUB-ASSETS: naming one part of an image without cutting the file up.
//
//  A spritesheet holds many pictures in one file. Slice it and each cell gets
//  a name, and the pair is written as one path:
//
//      textures/hero.png            the sheet
//      textures/hero.png#walk_1     one frame of it
//      textures/hero.png#walk       an animation, which is a list of frames
//
//  NO FILES ARE CREATED. A frame is cut out of the sheet's pixels when it is
//  loaded and handed to the graphics card like any other image, so a frame
//  works wherever an image works - a sprite, an interface picture, a thumbnail,
//  something dragged onto an entity - without any of them knowing it is one.
//
//  ==========================================================================
//  WHY '#' AND NOT '/'
//
//  Because "textures/hero.png/walk_1" is ambiguous: it could equally be a file
//  called walk_1 inside a real folder called hero.png, and telling the two
//  apart would mean asking the disk every time anybody looked at a path.
//
//  '#' is not a guarantee - it is legal in a filename on every platform this
//  engine runs on, and the editor itself can produce one, since PrefabPathFor
//  sanitises a name for the filesystem and had no reason to care about this.
//  So the rule below is deliberately NARROW rather than trusting the
//  character.
//
//  ==========================================================================
//  THE RULE: SPLIT AT THE LAST '#' WHOSE LEFT-HAND SIDE NAMES AN IMAGE.
//
//  That one sentence is what makes the two cases tell themselves apart with no
//  disk access at all:
//
//      textures/hero.png#walk_1   -> "textures/hero.png" is an image. A frame.
//      textures/hero#2.png        -> "textures/hero" is not. A plain file.
//      prefabs/Boss#2.oprefab     -> "prefabs/Boss" is not. A plain file.
//      textures/a.png#b.png#c     -> "textures/a.png#b.png" is an image, so
//                                    the sheet is that and the frame is "c".
//
//  And for the one case the rule still cannot settle - a real file on disk
//  genuinely named "a.png#b" - ResourceManager resolves the sub-asset first
//  and falls back to loading the literal path when the sheet has no such
//  frame. That check costs a disk lookup only on a path that was going to
//  fail anyway.
// ===========================================================================

inline constexpr char kSubAssetSeparator = '#';

// Does this path name an image file this engine can decode? A pure extension
// test; it does not touch the disk.
bool LooksLikeImage(std::string_view path);

// Is this path a sub-asset reference, by the rule above?
bool IsSubAsset(std::string_view path);

// The sheet a sub-asset belongs to - or the path itself, unchanged, when it is
// not one.
//
// NEVER EMPTY FOR A NON-EMPTY INPUT, which is what lets every caller use it
// unconditionally instead of asking IsSubAsset first and then branching. Most
// of the places that need this are asking "which FILE does this path involve?"
// and the answer for an ordinary path is itself.
std::string SheetPathOf(std::string_view path);

// The part after the separator, or an empty string when there is none.
std::string SubAssetNameOf(std::string_view path);

// Builds "sheet#name". Returns an empty string when `name` is not legal, so a
// caller cannot accidentally build a path out of something that would escape
// the asset tree.
std::string MakeSubAssetPath(std::string_view sheetPath, std::string_view name);

// ---------------------------------------------------------------------------
//  What to CALL this asset on screen: the file's own name, plus the part
//  inside it when the path names one.
//
//      "textures/Sprites/Blue/hero.png"       -> "hero.png"
//      "textures/Sprites/Blue/hero.png#walk"  -> "hero.png#walk"
//      ""                                     -> ""
//
//  ==========================================================================
//  WHY THE FOLDERS GO AND THE SHEET'S NAME STAYS
//
//  A reference shown as its full path is unreadable in any field narrow enough
//  to sit in a panel: what you get is the middle of a path with both ends cut
//  off, and the end is the part that identifies it. The folders above it are
//  something the reader already knows, because they put the file there.
//
//  But the sheet's name is NOT dropped for a sub-asset, and that is the whole
//  reason this is not "everything after the last slash". A project with four
//  sliced sheets has four animations called "walk", and "walk" on its own
//  would be the same label on four different things.
//
//  HERE AND NOT IN THE EDITOR because it is a question about an asset path,
//  which is what this file is for - and because the editor cannot be reached
//  from the test binary, so anything that lived there could not be checked.
// ---------------------------------------------------------------------------
std::string AssetDisplayName(std::string_view path);

// ---------------------------------------------------------------------------
//  May a frame or an animation be called this?
//
//  THE GUARD IS ON THE NAME, NOT ON THE SEPARATOR, and that is the lesson of
//  the one genuinely dangerous bug this feature could have shipped. A name is
//  CONCATENATED INTO A PATH which is then handed to FileSystem::Resolve, and
//  Resolve lexically normalises - so a frame named "../../hero.png" resolves
//  to a real file somewhere above the project, and anything that deletes or
//  writes "the selected asset" would have found it. That hazard exists
//  whatever character joins the two halves, so it is refused here, once, at
//  the only point a name is written down.
//
//  Refused: an empty name, leading or trailing spaces, and any of
//  '/', '\', '#', ':' or '.'. The dot is refused as well as ".."  because a
//  name with a dot in it also makes the split rule above ambiguous, and
//  because nothing wants it: these are names like "walk_1", not filenames.
//
//  `outReason` is written for a person, because it goes straight into the
//  Inspector beneath the box they typed in.
// ---------------------------------------------------------------------------
bool IsLegalSubAssetName(std::string_view name, std::string& outReason);

// ---------------------------------------------------------------------------
//  One sliced frame: a name, and the rectangle it occupies in the sheet.
// ---------------------------------------------------------------------------
struct SheetFrame {
    std::string name;
    int         x = 0;
    int         y = 0;
    int         w = 0;
    int         h = 0;

    // Defaulted, so that IsDefault below can keep comparing against a
    // default-constructed one instead of a written-out list of defaults that
    // would quietly stop describing this struct the day a field is added.
    bool operator==(const SheetFrame&) const = default;
};

// ---------------------------------------------------------------------------
//  A named run of frames.
//
//  `frames` holds FRAME NAMES rather than rectangles, so renaming a frame or
//  nudging its rectangle does not have to be chased through every animation
//  that uses it. A name that no longer matches any frame is skipped when the
//  animation is resolved, and said so once.
// ---------------------------------------------------------------------------
struct SheetAnimation {
    std::string              name;
    std::vector<std::string> frames;
    float                    fps  = 12.0f;
    bool                     loop = true;

    // ======================================================================
    //  ASKED TO PLAY AGAIN: CARRY ON, OR START OVER?
    //
    //  Only ever a question about the clip ALREADY LOADED - switching to a
    //  different animation always begins it at the beginning, because there
    //  is one playback position and it belonged to the clip being left.
    //
    //  A WALK CYCLE WANTS TO CARRY ON. Stop half way through a stride, walk
    //  off again, and restarting from the first frame is a visible hitch
    //  every time.
    //
    //  A SWING OF A SWORD WANTS TO START OVER, or the second swing begins
    //  half way through the first.
    //
    //  SO IT BELONGS TO THE ANIMATION, beside fps and loop, and for the same
    //  reason they do: it is a fact about what the clip IS, not about one
    //  entity playing it. Forty enemies sharing a walk cycle cannot disagree
    //  about whether walking is continuous - and one entity plays BOTH a walk
    //  and a swing from the same sheet through ONE component, so a setting on
    //  the component could not tell those two apart at all.
    //
    //  FALSE BY DEFAULT, which is what every clip did before this existed.
    //  A clip that ran to its END is rewound whatever this says: there is
    //  nothing to carry on with, and resuming would leave it on its last
    //  frame for ever.
    // ======================================================================
    bool resume = false;

    bool operator==(const SheetAnimation&) const = default;
};

// ---------------------------------------------------------------------------
//  How a sheet was sliced.
//
//  THE FRAMES ARE STORED EXPLICITLY rather than recomputed from the grid, and
//  that is the difference between a slicer and a grid setting: a frame moved
//  or resized by hand, or dropped altogether, has no grid that describes it.
//  The grid fields are kept beside them so the slicer can be re-run and so the
//  Inspector can show what was last asked for.
// ---------------------------------------------------------------------------
struct SheetSlice {
    int cellWidth  = 0;
    int cellHeight = 0;
    int margin     = 0;   // border skipped on all four sides
    int spacing    = 0;   // gap between neighbouring cells

    std::vector<SheetFrame>     frames;
    std::vector<SheetAnimation> animations;

    bool operator==(const SheetSlice&) const = default;

    bool IsSliced() const { return !frames.empty(); }

    // Null when there is no such frame or animation. Compares by name, which
    // is why a name is not allowed to be ambiguous - see
    // IsLegalSubAssetName.
    const SheetFrame*     FindFrame(std::string_view name) const;
    const SheetAnimation* FindAnimation(std::string_view name) const;
};

// ---------------------------------------------------------------------------
//  Will this slice survive being written down and read back?
//
//  ReadSheet IS FORGIVING ON PURPOSE - it skips and names each entry it cannot
//  use and keeps the rest, because a sidecar is a text file somebody may have
//  edited by hand and one bad frame should not cost them the sheet. That is
//  exactly the WRONG thing to discover about a file this editor has just
//  written: the frames would sit in the panel looking saved until the next
//  reload and then quietly be gone.
//
//  So the rules are written out here, once, and ImportLibrary::Save refuses
//  anything that breaks them. The reader stays forgiving, the writer is
//  strict, and the two cannot drift apart because this is the only statement
//  of what the reader will accept.
//
//  THE CONTRACT IS EXACTLY ROUND-TRIPPING, nothing wider. A nonsense frame
//  rate round-trips perfectly, so it is not refused here - the Inspector's own
//  widget keeps it positive and whatever plays an animation must cope anyway,
//  because a hand-written file can still say 0.
//
//  `outReason` names the first thing it objects to, in words meant for the
//  person who is about to read it in the Inspector.
// ---------------------------------------------------------------------------
bool IsLegalSheet(const SheetSlice& sheet, std::string& outReason);

// ---------------------------------------------------------------------------
//  The most frames one grid may be cut into.
//
//  NOT A MEMORY LIMIT - slicing writes rectangles into a JSON file and creates
//  no textures at all; those are cut one at a time, when something asks for
//  one. It is a TYPO LIMIT. "1" in the cell-width box of a 1024x1024 image
//  asks for a million rectangles, and a sidecar that size is a worse outcome
//  than being told the number is wrong.
//
//  64x64 cells of a 4096-pixel-square sheet, which is past anything real.
// ---------------------------------------------------------------------------
inline constexpr int kMaxSliceFrames = 4096;

// ---------------------------------------------------------------------------
//  What a grid comes to on an image of a given size.
//
//  Separated from the slicing itself because the Inspector needs these numbers
//  EVERY FRAME, to draw the grid over the preview and to say how many frames
//  the button would make. Building the frame vector that often would be an
//  allocation storm for a result that is thrown away unseen.
// ---------------------------------------------------------------------------
struct SliceGeometry {
    int columns = 0;
    int rows    = 0;

    // Pixels past the last whole cell, inside the margin. ALMOST ALWAYS A
    // MISTAKE WORTH SHOWING: a sheet exported by a tool divides exactly, so a
    // remainder usually means one of the four numbers is wrong. It is not an
    // error, because trailing blank space in a hand-made sheet is real.
    int leftoverX = 0;
    int leftoverY = 0;

    // columns * rows, which is why it is not an int: the product of two
    // plausible-looking numbers can leave the range of one.
    long long count = 0;

    // Empty when count is positive; otherwise why there is nothing to cut.
    std::string reason;
};

SliceGeometry MeasureGrid(int imageWidth, int imageHeight, const SheetSlice& grid);

// ---------------------------------------------------------------------------
//  Cut a grid of frames out of an image of this size.
//
//  PURE ARITHMETIC OVER INTEGERS, and deliberately not a member of anything.
//  No texture, no renderer, no file - which is what makes the awkward cases
//  testable with no window on screen: a cell of zero, a grid that does not
//  divide, a margin bigger than the image, a count that runs to millions.
//
//  `grid` is read for cellWidth, cellHeight, margin and spacing only; any
//  frames and animations already in it are ignored, because this answers
//  "what would this grid cut?" and not "what is cut now?".
//
//  Frames come back in ROW-MAJOR order named `namePrefix` followed by the
//  index - "f0", "f1", ... - because the sheet's own name is already in the
//  path that addresses them: "textures/hero.png#f3".
//
//  Empty with `outReason` set when the grid cuts nothing, when it would cut
//  more than kMaxSliceFrames, or when the prefix cannot be part of a name.
//  A grid that does not divide the image exactly still slices: see
//  SliceGeometry::leftoverX for how that is reported instead.
// ---------------------------------------------------------------------------
std::vector<SheetFrame> SliceGrid(int imageWidth, int imageHeight,
                                  const SheetSlice& grid,
                                  std::string_view namePrefix,
                                  std::string& outReason);

// ---------------------------------------------------------------------------
//  A first guess at a cell size, from the image's dimensions AND NOTHING ELSE.
//
//  THAT IS A REAL LIMIT AND IT IS SAID ON SCREEN TOO. What people mean by
//  auto-detect is finding islands of opaque pixels and putting a rectangle
//  round each; this cannot do that, because the editor has no decoder of its
//  own and no CPU copy of a texture - the pixels went to the GPU, and an
//  SDL_Texture made from a file cannot be read back. All that is left is the
//  width and the height.
//
//  So it answers the largest square cell that divides the image exactly both
//  ways and leaves more than one cell: right for the grid sheets every
//  exporter produces, wrong for a sheet with uneven padding, and never a
//  substitute for looking at the preview.
//
//  False when the dimensions suggest nothing at all - a prime width, or an
//  image too small to be a grid. The caller then leaves the numbers alone and
//  says so rather than inventing a plausible-looking 16.
// ---------------------------------------------------------------------------
bool GuessSheetCell(int imageWidth, int imageHeight, int& outCell);

// ---------------------------------------------------------------------------
//  TextureImport - everything a .oimport can say about an image.
//
//  Kept small and kept COPYABLE. ImportLibrary hands these out by value rather
//  than by reference into its cache: the struct is one field, so a copy costs
//  nothing worth measuring, and a reference into a cache that can be cleared
//  is a lifetime question nobody should have to think about in order to ask how
//  a sprite should be scaled.
//
//  ==========================================================================
//  WHICH SETTINGS EARN A PLACE IS A REAL QUESTION, and the bar is that the
//  engine must be able to honour it. Premultiplied alpha was written and then
//  taken back out for failing that bar twice over: nothing in the engine
//  premultiplies anything, so the flag would have had no producer, and
//  Renderer::DrawSprite's tint scales alpha without scaling colour, which
//  breaks the premultiplied invariant for any tint that is not fully opaque.
//  A setting that is recorded and not honoured is worse than a missing one,
//  because somebody will set it and believe it.
//
//  ==========================================================================
//  THIS IS NO LONGER CHEAP TO COPY, AND IT IS STILL RETURNED BY VALUE.
//
//  `sheet` holds a vector per sliced sheet, so a copy of this struct is a few
//  kilobytes for a heavily sliced one rather than the single enum it used to
//  be - and the Inspector asks for it on every frame it draws. That is a
//  deliberate trade: a reference into ImportLibrary's cache would be faster
//  and would come with a lifetime nobody should have to reason about in order
//  to ask how a sprite should be scaled.
//
//  If it ever measures, the fix is an accessor that hands back a pointer with
//  a stated lifetime - not making this struct cheap again by taking the sheet
//  out of it, which is where it belongs.
// ---------------------------------------------------------------------------
struct TextureImport {
    // Nearest is what the engine did before this existed. See the note above
    // on why every default here is yesterday's behaviour.
    TextureFilter filter = TextureFilter::Nearest;

    // Empty unless this image has been sliced. An unsliced image has no
    // sub-assets, which is what makes every project that never opens the
    // slicer behave exactly as it did.
    SheetSlice sheet;
};

// Do these settings say anything a default would not? Save uses it to decide
// whether the sidecar should exist at all.
bool IsDefault(const TextureImport& settings);

// ---------------------------------------------------------------------------
//  FontImport - the same question, asked of a .ttf.
//
//  A font atlas is an image too, and the same choice applies: a font drawn as
//  pixel art wants Nearest, and every other font wants Linear. It is a
//  separate struct rather than being folded into TextureImport because the two
//  have different DEFAULTS, and one struct whose default depends on who is
//  asking is worse than two structs.
// ---------------------------------------------------------------------------
struct FontImport {
    // Linear is what the atlas baker did before this existed: it passed
    // `smooth = true`, with the argument for it written out in Font.cpp.
    TextureFilter filter = TextureFilter::Linear;
};

bool IsDefault(const FontImport& settings);

// ---------------------------------------------------------------------------
//  ImportLibrary - reads the .oimport files, and keeps the sidecars tidy.
//
//  The same shape as TileLibrary and ResourceManager: ask for a path, get the
//  settings; ask again, get them from the cache rather than off the disk.
//
//  ==========================================================================
//  READING NEVER FAILS, AND THAT IS A DECISION RATHER THAN AN OMISSION.
//
//  There is no error out-parameter on ForTexture or ForFont. A missing
//  sidecar, an unreadable one, one that is not JSON, and one naming a filter
//  this engine has never heard of all mean the same thing in practice - use
//  the defaults - and there is no caller for whom the right response to any of
//  them is to stop drawing. Each one is logged, naming the file, and the
//  picture still appears.
//
//  That is the OPPOSITE of how TileAsset::Deserialize treats a bad frameMode,
//  which refuses and names the accepted values. The difference is that a tile
//  asset with a broken field is not a tile asset, whereas an image with broken
//  settings is still an image, and the worst thing this file could do is turn a
//  typo in a settings file into a level that will not load.
// ---------------------------------------------------------------------------
class ImportLibrary {
public:
    // "textures/player.png" -> "textures/player.png.oimport". A pure string
    // operation: it touches no disk and does not care whether either file
    // exists.
    //
    // APPENDED, NEVER SUBSTITUTED. Doing this with
    // std::filesystem::path::replace_extension would turn "hero.idle.png" into
    // "hero.idle.oimport" and "hero.png" and "hero.jpg" into the same file.
    static std::string SidecarPath(std::string_view assetPath);

    // Is this path itself a sidecar? Asked by the Assets panel, which hides
    // them, by the importer, which refuses them, and by the exporter, which
    // must not derive a sidecar's sidecar.
    static bool IsSidecar(std::string_view path);

    // Does this asset have settings of its own, as opposed to running on the
    // defaults?
    //
    // FOR THE EDITOR, which says so in the Inspector and offers to revert.
    // Nothing that DRAWS should ask: it would be a second answer to a question
    // ForTexture already answers, and two answers can disagree. It is also a
    // weaker question than it looks in an exported game - see the note on the
    // read path in the .cpp.
    static bool HasSettings(std::string_view assetPath);

    static TextureImport ForTexture(std::string_view assetPath);
    static FontImport    ForFont(std::string_view assetPath);

    // ======================================================================
    //  Writes the sidecar - or DELETES it, when `settings` are the defaults.
    //
    //  The deletion is the interesting half. Without it, choosing "Pixel Art"
    //  on an image that was already going to be scaled that way would write a
    //  file saying so, and a project would end up with one redundant sidecar
    //  per image. With it, the absence of a file is the one spelling of "the
    //  defaults", which is also what makes the Assets panel's hiding of these
    //  files harmless: there is nothing hidden for the overwhelming majority
    //  of assets.
    //
    //  What it costs: there is no way to PIN a setting that happens to equal
    //  today's default against a future change of that default. That is a
    //  price worth paying for not writing sixty files that all say nothing.
    //
    //  Returns false with a reason, because this one CAN fail in a way the
    //  caller must report - a read-only file, a full disk - and silently
    //  losing a setting somebody has just chosen in the Inspector is exactly
    //  the sort of thing that teaches people not to trust a tool.
    // ======================================================================
    //  A SUB-ASSET PATH IS REFUSED, with a reason. '#' is a legal filename
    //  character, so "textures/hero.png#walk_1.oimport" is a file that WOULD
    //  be written, and it would be a permanent orphan that nothing ever reads
    //  and the Assets panel hides. A frame's settings are its sheet's; see
    //  SheetPathOf.
    static bool Save(std::string_view assetPath, const TextureImport& settings,
                     std::string& outError);
    static bool Save(std::string_view assetPath, const FontImport& settings,
                     std::string& outError);

    // Deletes the sidecar: "back to the defaults".
    //
    // SUCCEEDS WHEN THERE WAS NOTHING TO DELETE. The caller asked for an end
    // state and that end state has been reached; reporting a failure for a job
    // already done is how a Revert button comes to look broken on the one
    // asset that never had settings in the first place. Same argument as rule
    // 3 in scene/DeferredOps.h - destroying something twice is harmless.
    static bool Revert(std::string_view assetPath, std::string& outError);

    // ======================================================================
    //  KEEPS THE SIDECAR BESIDE ITS ASSET. Call it AFTER the asset itself has
    //  been renamed, moved or deleted; an empty `newAssetPath` means the asset
    //  is gone and the sidecar should go with it.
    //
    //  ONE DOOR, AND THAT IS THE POINT. There are three routes in the editor -
    //  rename, move, delete - and a sidecar left behind by any one of them is
    //  somebody's settings silently lost plus an orphan file that nothing will
    //  ever mention again. Three call sites into one function can be checked;
    //  three copies of the logic cannot. It is the same argument DestroyEntity
    //  makes in editor/src/EditorApp.h, where three of four destroy sites
    //  remembered to mark the scene and the fourth did not.
    //
    //  A FOLDER NEEDS NO CALL AND IS HARMLESS IF IT GETS ONE. Renaming or
    //  moving a folder carries everything inside it, sidecars included,
    //  because they are ordinary files that live in it. Passing a folder here
    //  looks for "<folder>.oimport", does not find it, and does nothing - so
    //  callers do not have to work out which they are holding, and none of
    //  them has to carry an isDirectory flag it would otherwise need.
    //
    //  A SIDECAR ALREADY AT THE DESTINATION IS OVERWRITTEN. It can only be an
    //  orphan: the asset operation that got us here has already succeeded, and
    //  FileSystem::Rename and ::Move both refuse a name that is taken, so
    //  nothing it could have described is still there. Refusing instead would
    //  leave the arriving asset wearing a deleted asset's settings, which is
    //  the worse of the two wrongs.
    // ======================================================================
    static void FollowAsset(std::string_view oldAssetPath,
                            std::string_view newAssetPath);

    // ======================================================================
    //  Forgets what is cached, so the next question goes to the disk.
    //
    //  THERE IS A CACHE, AND IT IS THE INSPECTOR THAT MAKES IT NECESSARY: the
    //  panel asks for the selected asset's settings on every frame it draws,
    //  and without a cache that is a file read sixty times a second for as
    //  long as somebody leaves an image selected.
    //
    //  The consequence is that a .oimport edited by hand WHILE THE EDITOR IS
    //  OPEN is not noticed until this is called. Everything done through the
    //  editor keeps the cache in step as it goes, so this exists for the
    //  outside edit - which is a real case, because the whole argument for
    //  JSON is that these files can be opened and fixed by hand. The editor
    //  calls it when its window regains focus, beside the same check for
    //  scripts and prefabs; see RebuildScriptsIfChanged in
    //  editor/src/EditorApp.h.
    // ======================================================================
    // ======================================================================
    //  CHANGES WHENEVER AN ANSWER FROM THIS LIBRARY MIGHT HAVE CHANGED.
    //
    //  One integer, starting at 1 and only rising, moved by every write to
    //  the cache: Save (both the written path and the back-to-defaults one),
    //  Revert, the re-keying FollowAsset carries, and Clear. Nothing can
    //  change a .oimport through any other door, so a holder that remembers
    //  the value it read at can decide whether to ask again with ONE COMPARE
    //  - and a new door would have to bump it here, beside the cache write it
    //  is already making. The READ path's insert does not bump it: reading
    //  the disk is not a change.
    //
    //  WHO NEEDS IT: anything that resolved something out of a sheet and KEPT
    //  the result. A sprite showing one frame re-reads the moment anybody
    //  sets its texture, so it never needed this. A playing animation holds
    //  twelve frames and asks nobody, so without this it would go on playing
    //  the old rectangles for ever - InvalidateSubAssets erases cache ENTRIES
    //  and cannot reach a texture something already holds, because the
    //  texture cache is weak.
    //
    //  WHY A COUNTER AND NOT A NOTIFICATION. There is no observer channel
    //  anywhere in this engine, the only thing that re-slices is the editor,
    //  and a holder that ASKS cannot be forgotten by a future caller the way
    //  a list of observers can. It also works in an exported game, where no
    //  editor code exists to do the telling.
    //
    //  DELIBERATELY NOT PER ASSET. A holder that re-reads because an
    //  unrelated image was saved pays one map lookup and one comparison; a
    //  holder that MISSES a change shows the wrong picture with nothing to
    //  explain it. The cheap mistake is the one to allow.
    // ======================================================================
    static std::uint64_t Generation();

    static void Clear();

    static std::size_t CachedCount();
};

} // namespace eng
