// ============================================================================
//  AnimationRef.cpp - the two accessors that cut a sub-asset path apart.
//
//  Out of line for one reason: it is these two functions that need
//  resource/ImportSettings.h, and AnimationRef.h is included by
//  scene/ScriptFields.h, which is included by Engine.h, which is the single
//  header in front of every script a project compiles. Putting the importer
//  on that path would cost every script its compile time for two string cuts.
// ============================================================================

#include <engine/scene/AnimationRef.h>

#include <engine/resource/ImportSettings.h>

namespace eng {

std::string AnimationRef::Sheet() const {
    // EMPTY FOR A BARE NAME, which is the question "do I know which sheet?"
    // answered honestly rather than a guess. SheetPathOf is asked only once
    // IsSubAsset has agreed there is a '#' to cut at - it is documented as
    // safe to call unconditionally, but asking in this order is what makes
    // the two cases readable here.
    return IsSubAsset(m_path) ? SheetPathOf(m_path) : std::string{};
}

std::string AnimationRef::Clip() const {
    // THE WHOLE STRING WHEN THERE IS NO '#', which is where this parts company
    // with SubAssetNameOf on purpose. See the note in the header: a bare name
    // IS a clip name, and answering "" for it would hand the caller back the
    // raw string and the branch that goes with it.
    return IsSubAsset(m_path) ? SubAssetNameOf(m_path) : m_path;
}

} // namespace eng
