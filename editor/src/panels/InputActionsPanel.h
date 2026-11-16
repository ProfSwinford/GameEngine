#pragma once

// ============================================================================
//  InputActionsPanel - the Actions editor, modelled on Unity's.
//
//  Three panes side by side, which is Unity's own arrangement and is worth
//  copying because the shape of the data is the same:
//
//      ACTION MAPS  |  ACTIONS and their bindings  |  PROPERTIES
//
//  "+" on each adds at that level. The Properties pane shows whatever is
//  selected in the middle - an action's type and control type, or a
//  binding's control and its composite parts - and its title changes to say
//  which.
//
//  ==========================================================================
//  IT EDITS A COPY AND SAVES THROUGH THE FILE
//
//  Not the asset InputActionLibrary is handing out. Two reasons, and the
//  second is the one that matters:
//
//    A file being edited is broken most of the time. Half a renamed action
//    is the usual state of one of these while somebody is typing, and the
//    running scene is pointing at the live asset.
//
//    POINTERS INTO AN ASSET DIE WHEN IT CHANGES. Adding an action moves the
//    vector, so every InputAction* a component cached would dangle. Editing
//    a copy and then going through Save -> InputActionLibrary::Reload means
//    the one code path that already knows how to do that safely is the one
//    that does it, rather than a second path written here.
//
//  ==========================================================================
//  IT NAMES THE METHOD EACH ACTION WILL CALL, which Unity does not.
//
//  "Jump" calls OnJump, and the rule that turns one into the other drops
//  punctuation and upper-cases only the first letter - so "move camera"
//  calls OnMovecamera, which is surprising the first time and invisible
//  until nothing happens. Showing it beside the name costs one line and
//  removes the whole class of question.
// ============================================================================

#include "Panel.h"

#include <engine/input/InputActionAsset.h>

#include <string>
#include <vector>

namespace editor {

class InputActionsPanel final : public Panel {
public:
    const char* Title() const override { return "Input Actions"; }
    void        Draw() override;

    // THE REQUEST IS TAKEN HERE TOO, and that is the whole reason this is
    // overridden. The window starts closed, and a closed panel never draws -
    // so a double-click in the Assets panel would leave its request sitting
    // there forever, and the window somebody asked for would never appear.
    void OnHidden() override;

    // Opens a file in the window, for the Assets panel's double-click.
    void Open(std::string_view virtualPath);

    // ======================================================================
    //  MAKES A NEW FILE, with the engine's default action maps already in it.
    //
    //  STATIC, so the Assets panel's "+ Input" button is the same one call
    //  rather than a second copy of the same four lines. Both then leave a
    //  requestedInputActions behind, so the new file opens in this window
    //  wherever it was made from.
    //
    //  NOT EMPTY, which is the whole point. A file with no maps in it is
    //  correct and useless: nothing to look at, nothing to play, and no
    //  example of the two shapes a binding can take. The default binds what
    //  config/engine.json binds, so a project is playable the moment it
    //  exists - see eng::DefaultActionsJson.
    //
    //  REFUSES TO OVERWRITE. "New" that silently replaced somebody's
    //  afternoon would be the worst button in the editor.
    // ======================================================================
    static bool CreateDefault(std::string_view virtualPath, std::string& outError);

private:
    void TakeOpenRequest();
    void DrawToolbar();
    void DrawNewPopup();
    void DrawMaps(float width);
    void DrawActions(float width);
    void DrawProperties();
    void DrawActionProperties(eng::InputAction& action);
    void DrawBindingProperties(eng::InputBinding& binding);
    void DrawControlField(const char* label, eng::Control& control, const char* id);

    bool Load(std::string_view virtualPath);
    void Save();

    eng::ActionMap*    SelectedMap();
    eng::InputAction*  SelectedAction();
    eng::InputBinding* SelectedBinding();

    // WHICH FILE IS OPEN, and the edit buffer for it. The asset is
    // non-copyable, so it is held by value and deserialized into.
    std::string           m_path;
    eng::InputActionAsset m_asset;
    bool                  m_loaded = false;
    bool                  m_dirty  = false;
    std::string           m_error;

    // WHAT SAVING WOULD LOSE - eng::InputActionAsset::Validate's answer,
    // recomputed every frame so the window says so while it is still true
    // rather than when Save is pressed.
    //
    // ITS OWN FIELD AND NOT m_error, which holds the last thing that actually
    // went wrong - a key that would not parse, a file that would not load -
    // and must not be stamped on by a check that runs on every frame.
    std::string m_problems;

    // The selection. Indices and not pointers, because every "+" and every
    // "-" moves the vectors underneath them.
    int m_map     = 0;
    int m_action  = -1;
    int m_binding = -1;   // -1 means the action itself is selected

    // Refreshed when the window opens rather than every frame: listing a
    // folder is a disk walk, and nobody adds a .oinput while staring at the
    // dropdown.
    std::vector<std::string> m_files;
    bool                     m_filesListed = false;

    // The New dialog. A char array because ImGui edits those, and prefilled
    // with a name that is already a reasonable answer - somebody making their
    // first one should be able to press Enter.
    bool m_openNew = false;
    char m_newName[128] = "PlayerControls";
    std::string m_newError;
};

} // namespace editor
