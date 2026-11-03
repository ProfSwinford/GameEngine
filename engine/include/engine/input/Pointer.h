#pragma once

// ============================================================================
//  Pointer.h - where the mouse is, in whichever space you need it.
//
//  ==========================================================================
//  WHY THIS EXISTS, AND IT IS NOT CONVENIENCE
//
//  "Where is the mouse" has three different answers and only one of them is
//  the one game code wants:
//
//    WINDOW pixels      EventPump::MouseX/MouseY, measured from the top-left
//                       of the whole window. This is what the operating system
//                       reports.
//
//    VIEW pixels        the same point measured from the top-left of the
//                       picture the game is drawn into.
//
//    WORLD units        where that point is in the scene, which is what
//                       "which tile did I click" and "aim at the cursor"
//                       actually mean.
//
//  IN A SHIPPED GAME THE FIRST TWO ARE THE SAME, and that is the trap. The
//  window IS the view, so a script written against EventPump works perfectly -
//  until the same scene is run in the editor's Game view, where the view is a
//  panel inside the window and the two differ by the whole width of the
//  panel's border and title bar. The script then picks the wrong tile, by a
//  constant offset, only in the editor. Nothing reports it, and the natural
//  conclusion is that the camera maths is wrong.
//
//  The engine already solved this once, for the UI: scene/UI.h's SetPointer is
//  called by whoever owns the view - in the editor the Game panel, which
//  subtracts its own corner - so the UI's idea of the pointer is correct in
//  both. THIS FILE READS THAT, rather than the window, which is the whole
//  reason it is a separate header instead of two lines at a call site.
//
//  ==========================================================================
//  WHAT IS DELIBERATELY NOT HERE, so that nobody looks for it
//
//  No GetKey(KeyCode) and no mouse-button-by-number. Input in this engine goes
//  through NAMED ACTIONS - see input/InputMap.h - so a click is
//  InputMap::IsPressed("Fire") and the player can rebind it. A raw
//  button query would be a way around that, and the first game that ships with
//  a hardcoded button is what the action model exists to prevent.
//
//  What IS here is POSITION, because a position cannot be rebound and has no
//  equivalent in the action model at all.
// ============================================================================

#include <engine/math/Vec2.h>

namespace eng {

class Pointer {
public:
    // Is the pointer over the game's view at all?
    //
    // FALSE IS A REAL ANSWER AND IT MATTERS. In the editor the mouse spends
    // most of its time over some other panel, and in a shipped game it can
    // leave the window. Anything that follows the cursor should check this
    // first, or it will snap to the last place the mouse was seen and sit
    // there - which looks like the input being stuck.
    static bool IsOverView();

    // In the pixels of the view the game is drawn into, measured from the
    // TOP-LEFT - the same convention the renderer and the UI use.
    static Vec2 InView();

    // In world units. THIS IS THE ONE GAMEPLAY CODE WANTS.
    //
    //     if (eng::InputMap::IsPressed("Fire")) {
    //         Transform()->LookAt(eng::Pointer::InWorld());
    //     }
    //
    // Goes through the live camera, so it is correct for wherever the camera
    // is and whatever it is zoomed to. With no engine running - in a test -
    // it answers the view position unchanged, because there is no camera to
    // ask and the honest fallback is the identity rather than a guess.
    static Vec2 InWorld();

    // How big that view is, in pixels. What a HUD laid out by hand needs.
    static Vec2 ViewSize();

    // The button, as state and as the two edges.
    //
    // ONE STEP OF HYSTERESIS, deliberately: these are worked out from this
    // simulation step against the previous one, so they are stable for the
    // whole step however many scripts ask and whenever they run. Asking in
    // OnUpdate rather than OnFixedUpdate is still fine - it answers the most
    // recent step's edge - but a click is a gameplay event and OnFixedUpdate
    // is where it belongs.
    static bool IsDown();
    static bool WentDown();
    static bool WentUp();
};

} // namespace eng
