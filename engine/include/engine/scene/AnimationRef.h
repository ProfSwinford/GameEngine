#pragma once

// ============================================================================
//  AnimationRef.h - "which animation should this script play?", asked in the
//  Inspector rather than typed into the code.
//
//  ==========================================================================
//  THE PROBLEM IT SOLVES, which is PrefabRef's plus two of its own
//
//  Play takes the name of a clip, so the obvious way to write a walk cycle is:
//
//      std::string walkLeft = "walk_left";
//
//  A std::string IS an exposed script variable, so that appears in the
//  Inspector - as a plain text box. The four costs PrefabRef.h lists apply
//  here word for word: nothing checks it, nothing offers the clips that exist,
//  renaming the animation in the slicer breaks it silently, and you cannot
//  drag a clip onto it.
//
//  THE ANSWER UNTIL NOW WAS A COMMENT:
//
//      std::string walkLeft;   // [animation] which clip to play
//
//  which does produce the slot. But an animation was the only thing in this
//  engine designated that way, so it was the one nobody could guess and the
//  one a second array added beside the first silently did not get. That was
//  reported, in those words, and it is the reason this file exists: a marker
//  at the far right of a declaration is not where meaning belongs. A TYPE is.
//
//  The marker still works - see AssetFieldKind in ScriptFields.h - because
//  deleting it would break scripts that are correct today. It is the older
//  spelling now, not the only one.
//
//  ==========================================================================
//  AND THE COST THAT IS THIS TYPE'S OWN: A NAME AND A PATH BOTH END UP IN
//  THAT BOX, AND THEY ARE NOT INTERCHANGEABLE.
//
//  Dragging a clip out of the Assets panel gives
//  "textures/hero.png#walk_left". Typing gives "walk_left". Both arrive, and
//  the two want DIFFERENT calls: Play(name) keeps the frames the component has
//  already cut and restarts the clip, where SetAnimationPath(path) re-reads
//  the sheet and re-loads every frame.
//
//  So every script holding one had to tell them apart. There were twenty-five
//  lines of it in assets/scripts/TilemapWalker.cpp, waiting to be written
//  again by the next person who animated a character - AND THEY WERE SUBTLY
//  WRONG, in a way nothing could have caught from inside a script: they
//  compared against SpriteAnimationComponent::SheetPath(), which is EMPTY
//  until something has asked ResolvedFrames(). On an entity that is not
//  playing yet nothing has, so the comparison always failed and every change
//  of direction took the expensive branch - the exact cost the comment above
//  it said it was avoiding.
//
//  That reasoning now lives in SpriteAnimationComponent::Play(const
//  AnimationRef&), once, where the sheet is already known and where a test can
//  reach it.
//
//  ==========================================================================
//  WHY A PATH, AND NOT THE CLIP'S OWN DATA
//
//  eng::SheetAnimation - the frame list, the fps, the loop and resume flags -
//  already exists, in the sheet's .oimport sidecar. It is deliberately NOT
//  what this holds, for three reasons:
//
//    it carries no sheet path, so it cannot say WHICH sheet's "walk" it is;
//
//    a script variable is written into the scene file, and copying a frame
//    list and an fps out of the sidecar into a .oscene is the duplication
//    SpriteAnimation.h argues against by name;
//
//    and it holds a std::vector<std::string>, so there is nothing
//    single-valued for the Inspector to draw and nothing one-line for the
//    file to store. A script field has to be ONE SCALAR - see the note on
//    Path() below for what breaks otherwise.
//
//  A path is also always answerable. The sheet may not be loaded, may not
//  exist yet, and is re-read wholesale whenever somebody re-slices it, so a
//  pointer would be stale after any of those with nothing to say so.
//
//  SO IT IS NOT VALIDATED HERE. A reference to a clip that does not exist is
//  a perfectly ordinary state for a level in the middle of being built; the
//  Inspector paints it red and playing it reports why, and neither refuses.
//  The same rule PrefabRef and EntityRef follow, for the same reason.
// ============================================================================

#include <string>
#include <string_view>

namespace eng {

class AnimationRef {
public:
    AnimationRef() = default;

    // Deliberately NOT explicit.
    // `eng::AnimationRef walk{"textures/hero.png#walk_left"};` is how a script
    // states a sensible default in its own code, exactly as PrefabRef lets one
    // name a prefab.
    //
    // IT DOES NOT MAKE Play("walk") AMBIGUOUS against Play(std::string_view).
    // Arriving here from a const char* or a std::string would take TWO
    // user-defined conversions and an implicit conversion sequence allows one,
    // so this overload is not even a candidate for either. Checked with a
    // compile, not assumed - see tests/src/test_scriptfields.cpp.
    AnimationRef(std::string_view subAssetPath) : m_path(subAssetPath) {}

    // ======================================================================
    //  THE SHEET AND THE CLIP AS ONE STRING - "textures/hero.png#walk_left" -
    //  which is exactly what the scene file stored when this was a
    //  std::string. That is what makes switching a script from one to the
    //  other cost nothing: see the note on migration in ScriptFields.h.
    //
    //  ONE SCALAR AND NOT TWO MEMBERS, and that is load-bearing rather than
    //  tidy. A prefab override is recorded per JSON LEAF - Prefab.cpp recurses
    //  into any object - so a {sheet, clip} pair would become two
    //  independently overridable fields, and a half-applied pair would name a
    //  clip in the wrong sheet. It would also invalidate every override
    //  already written at the old scalar path.
    //
    //  A BARE NAME IS ALSO VALID HERE, because the text box this type replaces
    //  took one and scenes saved before it exists may still hold one. Clip()
    //  below is what makes the two shapes answerable the same way.
    // ======================================================================
    const std::string& Path() const { return m_path; }

    void Set(std::string_view subAssetPath) { m_path = subAssetPath; }
    void Clear() { m_path.clear(); }

    // Whether anything has been chosen. NOT whether the clip exists - this
    // type does not look, for the reason in the header. A blank one is a
    // direction a character simply does not have, which is left alone rather
    // than treated as a mistake.
    bool IsSet() const { return !m_path.empty(); }

    // ======================================================================
    //  THE TWO QUESTIONS EVERY CALLER WAS ASKING BY HAND, and the reason this
    //  is a class and not a typedef.
    //
    //  Sheet()  the sheet the clip lives in, or EMPTY when this holds a bare
    //           name and so does not say which sheet. "Empty" is the question
    //           "do I know the sheet?" answered, not a failure.
    //
    //  Clip()   the clip's own name, ALWAYS - "walk_left" whether this holds
    //           "textures/hero.png#walk_left" or just "walk_left".
    //
    //  THE SECOND CONTRACT IS DELIBERATELY NOT SubAssetNameOf's. That function
    //  returns empty for anything that is not a sheet#clip path, which is
    //  right for a function about paths and wrong for a question about this
    //  reference: a bare name is a clip name, and a caller that has to write
    //  `name.empty() ? whole : name` has been handed the raw string again.
    //  Getting that backwards is most of what the twenty-five lines in
    //  TilemapWalker were.
    //
    //  BY VALUE, NOT BY REFERENCE, because neither is a member - they are cut
    //  out of m_path on demand. Storing them would be a second copy of the
    //  truth to keep in step with Set().
    //
    //  Defined in AnimationRef.cpp so that this header stays as cheap to
    //  include as PrefabRef.h: <string> and <string_view> and nothing else.
    //  A project's scripts compile at /std:c++20, so every public header has
    //  to stay C++20-clean, and pulling resource/ImportSettings.h in here
    //  would put the whole importer in front of every script.
    // ======================================================================
    std::string Sheet() const;
    std::string Clip() const;

private:
    std::string m_path;
};

} // namespace eng
