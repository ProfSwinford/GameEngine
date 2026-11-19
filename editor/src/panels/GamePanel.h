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

#include "EditorGui.h"
#include "GameViewLayout.h"
#include "Panel.h"

#include <engine/Engine.h>

#include <cstddef>
#include <vector>

namespace editor {

class GamePanel final : public Panel {
public:
    const char* Title() const override { return "Game"; }
    void        Draw() override;

    // Behind another tab, so it does not have the keyboard whatever it decided
    // last frame. See Panel::OnHidden - this is the bug that hook exists for.
    // Closed, collapsed, or a background tab in its dock node - Draw did not
    // run, so the keyboard goes back to the editor and there is no rectangle
    // for a click to land in. The two are given up together: a stale
    // rectangle would post clicks on whatever is drawn there now to the game.
    void OnHidden() override {
        m_focused = false;
        EditorGui::ClearGameViewArea();
    }

    // Called by EditorApp after every panel has drawn. See ScenePanel.h for
    // why drawing the world cannot happen inside Draw().
    void RenderView();

    bool HasFocus() const { return m_focused; }

    // ======================================================================
    //  WHAT SHAPE THE GAME IS RENDERED AT, and how big it is shown.
    //
    //  Public because EditorSession writes it down: which aspect you were
    //  looking through is a fact about the person, like where the Scene view
    //  was pointing, and resetting it to Free Aspect on every launch would
    //  make it a setting nobody uses twice. The custom entries especially -
    //  retyping "1366:768" each session is worse than not having it.
    //
    //  See GameViewLayout.h for what the three kinds mean and why the scale
    //  is a separate number from the resolution.
    // ======================================================================
    const std::vector<GameViewMode>& Modes() const { return m_modes; }
    std::size_t ChosenMode() const { return m_chosen; }
    void        ChooseMode(std::size_t index);
    float       Scale() const { return m_scale; }
    void        SetScale(float scale) { m_scale = scale; }

    // How many of Modes() are built in. Everything at or past it is custom
    // and can be removed.
    std::size_t BuiltInCount() const { return m_builtInCount; }
    void        AddCustomMode(const GameViewMode& mode);

private:
    // The toolbar row above the picture: the camera, the aspect dropdown and
    // the scale. Split out because Draw was already the longest function in
    // the panel and this doubled it.
    void DrawControls();

    eng::RenderTarget m_target;
    bool              m_focused = false;

    std::vector<GameViewMode> m_modes        = BuiltInGameViewModes();
    std::size_t               m_builtInCount = m_modes.size();
    std::size_t               m_chosen       = 0;   // Free Aspect
    float                     m_scale        = kFitScale;

    // What the custom-entry box holds while it is being typed into.
    char m_customText[32] = "";
    char m_customError[96] = "";
};

} // namespace editor
