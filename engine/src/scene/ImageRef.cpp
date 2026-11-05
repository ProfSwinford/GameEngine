// ============================================================================
//  ImageRef.cpp - the three accessors that cut a virtual path apart.
//
//  Out of line for one reason: it is these that need resource/ImportSettings.h,
//  and ImageRef.h is included by scene/ScriptFields.h and scene/Component.h,
//  both of which are reached by Engine.h - the single header in front of every
//  script a project compiles. Putting the importer on that path would cost
//  every script its compile time for three string cuts.
// ============================================================================

#include <engine/scene/ImageRef.h>

#include <engine/resource/ImportSettings.h>

namespace eng {

std::string ImageRef::File() const {
    // NEVER EMPTY ONCE SET, and that is SheetPathOf's own documented contract
    // rather than something arranged here: it returns the path unchanged when
    // there is no '#' to cut at. A path with no part named IS the file.
    //
    // This is the accessor that reads the opposite way round from
    // AnimationRef::Sheet(), which answers "" for a bare name because a bare
    // name there is a clip and says nothing about a sheet. See the header.
    return SheetPathOf(m_path);
}

std::string ImageRef::Frame() const {
    // EMPTY FOR A WHOLE PICTURE, which is SubAssetNameOf's contract and is
    // the right one here - unlike AnimationRef::Clip(), where the whole string
    // had to be the answer. "No part named" is a state this type genuinely has
    // and a caller genuinely needs to tell apart.
    return SubAssetNameOf(m_path);
}

bool ImageRef::IsWholeImage() const {
    // Asked rather than derived from Frame(), so a condition does not build
    // and destroy a std::string to learn one bit.
    return !IsSubAsset(m_path);
}

} // namespace eng
