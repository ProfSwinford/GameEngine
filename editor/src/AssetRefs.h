#pragma once

// ============================================================================
//  AssetRefs.h - following an asset when it is renamed or moved.
//
//  THE PROBLEM. A path is written down in a dozen kinds of file: a scene's
//  sprite names a texture, a tilemap's palette names .otile files, a tile
//  names its sheet, an entity names a .oinput, a prefab instance names a
//  .oprefab, build.json names scenes. Renaming the file those point at used
//  to change nothing but the file, so every one of those references became a
//  path to nothing.
//
//  AND IT FAILS QUIETLY, which is what makes it worth real work rather than a
//  warning. A tilemap whose palette entry will not load keeps its cells -
//  they are numbers - and simply draws nothing where that tile was, with its
//  components gone too. You do not get an error about the rename; you get a
//  level with holes in it, some time later.
//
//  ==========================================================================
//  MATCHED BY VALUE, NOT BY KEY.
//
//  The rewrite walks every string in every asset file and changes the ones
//  that ARE the old path. It deliberately does not work from a list of known
//  keys - "texture", "palette", "sheet", "prefab" and so on - even though
//  that list exists and would be safer in the abstract.
//
//  Two reasons. A key list is a thing to forget: every new component that
//  names an asset has to remember to join it, and the failure of forgetting
//  is silent in exactly the way described above. And it cannot cover script
//  variables at all, which hold asset paths under names the script author
//  chose - the references most likely to be missed by hand and least likely
//  to be noticed when they break.
//
//  The risk the other way is a string that merely happens to equal an asset's
//  virtual path and is not a reference to it. A virtual path is a specific
//  thing - "tiles/overworld_tree.otile" - and prose that collides with one is
//  not a case worth designing around. The result says how many it changed so
//  a surprising number is visible rather than silent.
//
//  ==========================================================================
//  A FOLDER AND A SUB-ASSET COUNT AS MATCHES TOO.
//
//  Moving "tiles/" has to follow "tiles/grass.otile", and a frame of a sliced
//  sheet is written "textures/hero.png#walk_1" - so the match is the old path
//  followed by nothing, by "/", or by "#". Anything else is a different file
//  whose name merely starts the same way: "tiles2/" is not inside "tiles".
//
//  ==========================================================================
//  THE OPEN SCENE IS REWRITTEN IN MEMORY AS WELL AS ON DISK.
//
//  Its file is just another file and is rewritten with the rest - but the
//  scene being edited may have unsaved changes, and those live only in
//  memory. Saving afterwards would put the old paths straight back.
//
//  It goes through SaveToString / LoadFromString, which is the same round
//  trip undo takes for every snapshot and Play takes on every Stop, so it is
//  an affordable and well-travelled path rather than a new one.
// ============================================================================

#include <string>

namespace editor {

struct RefRewrite {
    int  files      = 0;   // files on disk that changed
    int  references = 0;   // individual paths rewritten, across all of them
    bool openScene  = false;   // the scene in memory held one too
};

// Points every reference to `oldPath` at `newPath` instead. Both are virtual
// paths. Safe to call when nothing refers to the asset: it then changes
// nothing and says so.
RefRewrite RewriteAssetReferences(const std::string& oldPath,
                                  const std::string& newPath);

// ---------------------------------------------------------------------------
//  THE CACHES THAT STILL ANSWER TO THE OLD PATH.
//
//  Nothing in this engine watches the disk. A .otile or a .oprefab that has
//  been renamed goes on resolving from memory for the rest of the session, so
//  the editor looks entirely fine and the stale path is written back into the
//  scene on the next save - the breakage then turns up on the next launch,
//  with nothing left to connect it to the rename.
//
//  Called for every rename and move, whatever kind of file it was: each of
//  these does nothing for a path it has never heard of, and the Assets panel
//  should not have to classify a file to tidy up after it.
// ---------------------------------------------------------------------------
void ForgetCachedAsset(const std::string& oldPath);

// One line for the status bar, phrased for a person: "12 references in 3
// files", or "nothing referred to it".
std::string DescribeRewrite(const RefRewrite& result);

} // namespace editor
