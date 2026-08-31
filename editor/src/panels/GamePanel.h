#pragma once
// =============================================================================
//  THE GAME VIEW - what a player would see.
//
//  Renders through Engine::GetCamera(), the GAME camera, into its own
//  RenderTarget, with NO debug draw and no editing conveniences - no grid, no
//  origin axes, no collider outlines. That absence is the point: the Scene view
//  shows you what is there, and the Game view shows you what ships.
//
//  ---------------------------------------------------------------------------
//  FOCUS AND INPUT, which is the part that is easy to get subtly wrong.
//
//  Pressing Play focuses this panel and hands it the keyboard, exactly the way
//  Unity does. "Hands it the keyboard" means two things at once, and doing only
//  one produces a view that looks focused and ignores every key:
//
//    - ImGui's keyboard navigation is turned OFF, or the arrow keys move
//      between widgets instead of moving the player.
//    - EditorGui::WantsKeyboard stops reporting capture, or EventPump marks
//      every key consumed and InputMap skips it.
//
//  Both live behind EditorGui::SetGameInputFocus, so the panel asks for focus
//  and cannot forget half of it.
//
//  Clicking the view also takes focus, and clicking anywhere else gives it
//  back - so the IDE stays usable while the game is running, which is the
//  behaviour that makes tuning a CVar mid-play possible.
//
//  THE FOCUS REQUEST ITSELF IS MADE BY EditorApp, BY NAME, and not by this
//  panel. The first version had the Toolbar raise a flag that this panel acted
//  on inside Draw(), which looked right and did nothing: when Play is pressed
//  the Game view is a BACKGROUND TAB, ImGui::Begin returns false, Draw is
//  never called, and the request was read on no frame at all. Focus had to be
//  requested from outside the panel that needs it.
// =============================================================================
#include "Panel.h"

#include <engine/Engine.h>

namespace editor {

class GamePanel final : public Panel {
public:
    const char* Title() const override { return "Game"; }
    void        Draw() override;

    // Behind another tab, so it does not have the keyboard whatever it thought
    // last frame. See Panel::OnHidden - this is the bug that hook exists for.
    void OnHidden() override { m_focused = false; }

    // Called by EditorApp after every panel is drawn - see ScenePanel for why
    // rendering cannot happen inside Draw().
    void RenderView();

    bool HasFocus() const { return m_focused; }

private:
    eng::RenderTarget m_target;
    bool              m_focused = false;
};

} // namespace editor
