#pragma once

// ============================================================================
//  ImageRef.h - "which picture should this script use?", asked in the
//  Inspector rather than typed into the code.
//
//  ==========================================================================
//  THE PROBLEM IT SOLVES, which is PrefabRef's word for word
//
//  The obvious way to let a script be given a picture is:
//
//      std::string icon = "textures/ui/key.png";
//
//  A std::string IS an exposed script variable, so that appears in the
//  Inspector - as a plain text box. The four costs PrefabRef.h lists all
//  apply: nothing checks it, nothing offers the images that exist, moving the
//  file in the Assets panel breaks it silently, and you cannot drag an image
//  onto it.
//
//  THE ANSWER UNTIL NOW WAS A COMMENT:
//
//      std::string icon;   // [image] shown when this is carried
//
//  which does produce the slot. AnimationRef.h has the argument for why a
//  marker at the far right of a declaration is the wrong place for meaning,
//  and it was made about `[animation]` because that is the one somebody hit.
//
//  `[image]` was in the same position with one difference that makes it worse
//  rather than better: NOTHING IN THIS PROJECT HAS EVER USED IT. It was a
//  documented mechanism with no example anywhere in assets/scripts, so the
//  only way to find it was to read the engine's own headers. A marker nobody
//  can guess and nobody has demonstrated is not a feature, it is a note. That
//  is what this file replaces.
//
//  The marker still works - see AssetFieldKind in ScriptFields.h - because
//  removing it would break scripts that are correct today, here and in
//  anybody else's project. It is the older spelling now, not the only one.
//
//  ==========================================================================
//  WHY THIS IS A SECOND TYPE AND NOT AnimationRef WITH A FLAG
//
//  They look like the same class twice: both hold one virtual path, both
//  accept the sheet#part shape, both refuse to validate themselves. The
//  temptation is one AssetRef carrying a kind.
//
//  IT IS THE WRONG SHAPE, because A BARE STRING MEANS OPPOSITE THINGS in the
//  two of them:
//
//      "hero.png"    an ImageRef:     the whole picture. COMPLETE.
//      "walk_left"   an AnimationRef: a clip name, in a sheet it does not
//                                     name. INCOMPLETE on purpose - the
//                                     component supplies the sheet.
//
//  So the accessors come out INVERTED rather than shared:
//
//      ImageRef::File()      never empty once set - a path with no '#' IS the
//                            file, so there is always an answer
//      AnimationRef::Sheet() EMPTY for a bare name - it genuinely does not
//                            say which sheet
//
//      ImageRef::Frame()     EMPTY for a whole picture - "no part named, the
//                            file itself" is the ordinary case
//      AnimationRef::Clip()  never empty once set - a bare name IS a clip name
//
//  One type would have to answer both contracts from one string with no way
//  to tell which was wanted, which means a flag, which means every caller
//  checking it. Two types, four accessors, and nothing to get wrong - and the
//  field type in ScriptFields.h is what the Inspector reads to know whether to
//  offer pictures or clips in the picker.
//
//  ==========================================================================
//  WHAT IT DOES NOT BUY, SAID PLAINLY
//
//  AnimationRef absorbed twenty-five lines of script-side reasoning, because
//  SpriteAnimationComponent::Play took a clip NAME and a dragged clip arrives
//  as a PATH, so every script holding one had to tell them apart.
//
//  THERE IS NO SUCH DECISION HERE. SpriteComponent::SetTexture has always
//  taken either shape - ResourceManager::LoadTexture splits a '#' path itself
//  and cuts the frame out - so no script ever had to branch. What this type
//  adds is the slot, the picker, the drop target and the red; the
//  SetTexture(const ImageRef&) overload beside it is a one-line forwarder, and
//  it exists so that a script can pass the type where it used to pass the
//  string rather than writing `.Path()` at every call.
//
//  ==========================================================================
//  WHY A PATH, AND WHY ONE STRING
//
//  Not a TextureRef: the image may not be loaded, may not exist yet, and is
//  re-read wholesale whenever somebody re-imports it, so a pointer would be
//  stale after any of those with nothing to say so. A path is always
//  answerable and is what the scene file has to store anyway.
//
//  ONE SCALAR AND NOT A {file, part} PAIR, which is load-bearing rather than
//  tidy: a prefab override is recorded per JSON LEAF - Prefab.cpp recurses
//  into any object - so a pair would become two independently overridable
//  fields, and a half-applied pair would name a frame in the wrong sheet.
//
//  SO IT IS NOT VALIDATED HERE. A reference to a picture that does not exist
//  is an ordinary state for a level in the middle of being built; the
//  Inspector paints it red and says which of the two things is wrong, and the
//  loader reports it by name. Neither refuses. The same rule PrefabRef,
//  EntityRef and AnimationRef follow, for the same reason.
// ============================================================================

#include <string>
#include <string_view>

namespace eng {

class ImageRef {
public:
    ImageRef() = default;

    // Deliberately NOT explicit.
    // `eng::ImageRef icon{"textures/ui/key.png"};` is how a script states a
    // sensible default in its own code, exactly as PrefabRef lets one name a
    // prefab.
    //
    // IT DOES NOT MAKE SetTexture("hero.png") AMBIGUOUS against
    // SetTexture(std::string_view). Arriving here from a const char* or a
    // std::string would take TWO user-defined conversions and an implicit
    // conversion sequence allows one, so this overload is not even a candidate
    // for either. Checked with a compile, not assumed - see
    // tests/src/test_scriptfields.cpp.
    ImageRef(std::string_view virtualPath) : m_path(virtualPath) {}

    // ======================================================================
    //  THE WHOLE PICTURE - "textures/ui/key.png" - or ONE PART of a sliced
    //  sheet - "textures/ui/items.png#key". Both are things somebody will
    //  drag onto the slot, and both are what SetTexture takes.
    //
    //  THE PART MAY BE A FRAME *OR* AN ANIMATION. Frames and animations share
    //  one namespace inside a sheet, and an animation resolves to the first of
    //  its frames that still exists, because "draw this animation as a
    //  picture" has a useful answer - see SheetSlice::FindImage. That is why
    //  this type and AnimationRef can both hold "items.png#key" and neither of
    //  them is wrong.
    //
    //  It is exactly what the scene file stored when this was a std::string,
    //  which is what makes switching a script from one to the other cost
    //  nothing: see the note on migration in ScriptFields.h.
    // ======================================================================
    const std::string& Path() const { return m_path; }

    void Set(std::string_view virtualPath) { m_path = virtualPath; }
    void Clear() { m_path.clear(); }

    // Whether anything has been chosen. NOT whether it exists - this type does
    // not look, for the reason in the header.
    bool IsSet() const { return !m_path.empty(); }

    // ======================================================================
    //  THE TWO QUESTIONS A CALLER HAS, and the reason this is a class rather
    //  than a typedef.
    //
    //  File()   the image file this involves - the sheet for a part, and the
    //           path itself for a whole picture. NEVER EMPTY once set, which
    //           is the inverse of AnimationRef::Sheet() and one of the reasons
    //           these are two types. It is the file to check for, to show, and
    //           to re-import.
    //
    //  Frame()  the part named inside it, or EMPTY for a whole picture -
    //           which is an answer and not a failure. "Empty" here means "the
    //           file itself", which is the ordinary case.
    //
    //  IsWholeImage()  the same question as `Frame().empty()` without building
    //           a string to throw away. Prefer it in a condition.
    //
    //  BY VALUE, NOT BY REFERENCE, because neither is a member - they are cut
    //  out of m_path on demand. Storing them would be a second copy of the
    //  truth to keep in step with Set().
    //
    //  Defined in ImageRef.cpp so that this header stays as cheap to include
    //  as PrefabRef.h: <string> and <string_view> and nothing else. A
    //  project's scripts compile at /std:c++20, so every public header has to
    //  stay C++20-clean, and pulling resource/ImportSettings.h in here would
    //  put the whole importer in front of every script.
    // ======================================================================
    std::string File() const;
    std::string Frame() const;
    bool        IsWholeImage() const;

private:
    std::string m_path;
};

} // namespace eng
