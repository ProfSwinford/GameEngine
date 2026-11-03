#pragma once

// ============================================================================
//  PrefabRef.h - "which prefab should this script spawn?", asked in the
//  Inspector rather than typed into the code.
//
//  ==========================================================================
//  THE PROBLEM IT SOLVES
//
//  Instantiate takes a path, so the obvious way to write a spawner is:
//
//      std::string bulletPrefab = "prefabs/bullet.oprefab";
//
//  A std::string IS an exposed script variable, so that does appear in the
//  Inspector - as a plain text box. Which means:
//
//    nothing checks the path, so a typo is a log line at the moment the gun
//    first fires rather than something visible while you are building the
//    level;
//
//    nothing offers the prefabs that exist, so you have to remember both the
//    folder and the extension;
//
//    renaming or moving the prefab in the Assets panel silently breaks it,
//    because no part of the engine knows that this particular string was
//    meant to be a file;
//
//    and you cannot drag the prefab onto it, which is how everybody expects
//    to answer this question.
//
//  This type is the same string with its MEANING attached. The Inspector draws
//  it as a slot you drop a prefab onto, it says so when the file is missing,
//  and the scene file stores exactly what the string version stored - so
//  switching a script from one to the other loses nothing.
//
//  ==========================================================================
//  WHY A PATH AND NOT A POINTER, which is the same argument EntityRef.h makes
//
//  The asset it names may not be loaded, may not exist yet, and is reloaded
//  wholesale whenever somebody edits it in another program - see
//  PrefabLibrary. A pointer would be stale after any of those and there would
//  be nothing to say so. A path is always answerable and is what the scene
//  file has to contain anyway.
//
//  SO IT IS NOT VALIDATED HERE. A reference to a prefab that does not exist is
//  a perfectly ordinary state for a level in the middle of being built; the
//  Inspector reports it, and spawning it reports it, and neither refuses. The
//  same rule EntityRef follows for an entity that is not in the scene yet.
// ============================================================================

#include <string>
#include <string_view>

namespace eng {

class PrefabRef {
public:
    PrefabRef() = default;

    // Deliberately NOT explicit. `eng::PrefabRef bullet{"prefabs/bullet.oprefab"};`
    // is how a script states a sensible default in its own code, exactly as
    // EntityRef lets one name an entity - see the note about scoreLabel in
    // ScriptComponent::RetargetReferences for why a default written in the
    // code matters.
    PrefabRef(std::string_view virtualPath) : m_path(virtualPath) {}

    const std::string& Path() const { return m_path; }
    void               Set(std::string_view virtualPath) { m_path = virtualPath; }
    void               Clear() { m_path.clear(); }

    // Whether anything has been chosen. NOT whether the file exists - this
    // type does not look, for the reason in the header.
    bool IsSet() const { return !m_path.empty(); }

private:
    std::string m_path;
};

} // namespace eng
