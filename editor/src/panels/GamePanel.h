#pragma once

// ============================================================================
//  GamePanel.h - the Game view: what a player would actually see.
//
//  It draws the world through the GAME camera into its own picture, with NO
//  editing helpers - no grid, no origin arrows, no collider outlines. That
//  absence is the whole point: the Scene view shows you what is THERE, and the
//  Game view shows you what SHIPS.
//
//  ==========================================================================
//  FOCUS AND THE KEYBOARD, which is the part that is easy to get subtly wrong.
//
//  Pressing Play focuses this panel and hands it the keyboard, exactly as
//  Unity does. "Hands it the keyboard" means two things at once, and doing
//  only one of them gives you a view that looks focused and ignores every key:
//
//    * ImGui's keyboard navigation has to be switched off, or the arrow keys
//      move between widgets instead of moving the player.
//    * The interface has to stop reporting that it wants the keyboard, or
//      every key press is marked as claimed and the input system skips it.
//
//  Both live behind EditorGui::SetGameInputFocus, so a caller asks for focus
//  and cannot forget half of it.
//
//  Clicking this view also takes focus, and clicking anywhere else gives it
//  back - which is what keeps the rest of the editor usable while the game is
//  running.
//
//  THE FOCUS REQUEST IS MADE BY EditorApp, NOT BY THIS PANEL. The obvious
//  version has the Toolbar raise a flag that this panel acts on inside Draw,
//  and it does nothing at all: when Play is pressed the Game view is a
//  BACKGROUND TAB, so Draw is never called and the request is read on no frame
//  ever. Focus has to be requested from outside the panel that needs it.
// ============================================================================

#include "Panel.h"

#include <engine/Engine.h>

namespace editor {

class GamePanel final : public Panel {
public:
    const char* Title() const override { return "Game"; }
    void        Draw() override;

    // Behind another tab, so it does not have the keyboard whatever it decided
    // last frame. See Panel::OnHidden - this is the bug that hook exists for.
    void OnHidden() override { m_focused = false; }

    // Called by EditorApp after every panel has drawn. See ScenePanel.h for
    // why drawing the world cannot happen inside Draw().
    void RenderView();

    bool HasFocus() const { return m_focused; }

private:
    eng::RenderTarget m_target;
    bool              m_focused = false;
};

} // namespace editor
