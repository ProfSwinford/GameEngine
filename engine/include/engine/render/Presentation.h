#pragma once

// ============================================================================
//  Presentation.h - what shape the finished game is, and what fills the rest
//  of the window.
//
//  THE PROBLEM. A game drawn straight onto the window shows more of the level
//  on a wider monitor, because a bigger viewport at the same zoom shows more
//  world - see render/Camera.h, where the zoom scales and the viewport only
//  centres. So every judgement about framing, about what is just off-screen,
//  about where the interface sits, was being made against whatever window the
//  developer happened to have open. A player with an ultrawide saw further
//  than one with a laptop, and neither saw what was designed.
//
//  Pinning the shape is the fix, and it has a consequence that has to be
//  handled rather than hoped about: the window will not be that shape. A
//  16:9 game on a 16:10 display has to put SOMETHING in the two spare strips,
//  and the only honest answer is BLACK. Anything else - stretching, cropping,
//  showing more world - is the problem coming back.
//
//  ==========================================================================
//  THREE MODES, matching the Game view's dropdown in the editor.
//
//    FREE        the game fills the window. What the engine did before this
//                existed, and still the default: a project that never asks
//                for a shape behaves exactly as it used to.
//
//    AN ASPECT   the shape is pinned, the size is not. The game is rendered
//                as large as the window allows at that shape and centred,
//                with black in the margins. A player on a bigger monitor
//                gets a bigger picture of the SAME framing.
//
//    A RESOLUTION  the game is rendered at exactly that many pixels and
//                scaled to fit. The strictest, and the one that makes pixel
//                art behave: at 480x270 everything lands on the same grid
//                whatever the display.
//
//  ==========================================================================
//  FULL SCREEN STILL WORKS, and letterboxing is what makes that true. Without
//  it, full screen on a differently-shaped display is the one case where a
//  pinned aspect has nowhere to go.
//
//  ==========================================================================
//  IT IS A SETTING, NOT A SCENE PROPERTY. config/engine.json's "presentation"
//  section, read once at start-up. A camera's framing is a property of the
//  game; which monitor it is being played on is not, and a scene file carried
//  between machines must not disagree with either.
// ============================================================================

#include <engine/math/Vec2.h>

#include <string>
#include <string_view>

namespace eng {

class Presentation {
public:
    enum class Mode {
        Free,
        Aspect,
        Resolution,
    };

    // Where the game goes in a window of this size, and what it renders at.
    //
    // THE TWO SIZES ARE DIFFERENT NUMBERS, which is the whole reason this is
    // a struct: `render*` is what the camera draws, `x/y/width/height` is
    // where that picture lands on the window. For Free they are the same and
    // nothing is letterboxed.
    struct Plan {
        int   renderWidth  = 1;
        int   renderHeight = 1;

        float x      = 0.0f;
        float y      = 0.0f;
        float width  = 1.0f;
        float height = 1.0f;

        // Is any of the window NOT the game? When false the game fills it
        // exactly and there is nothing to clear behind it.
        bool letterboxed = false;
    };

    static Mode CurrentMode();
    static bool IsFree() { return CurrentMode() == Mode::Free; }

    static void SetFree();

    // A SHAPE, as two whole numbers rather than a ratio, so that "16:9" round
    // trips through the settings file as the thing somebody wrote. Anything
    // zero or negative is refused and leaves the mode alone.
    static void SetAspect(int x, int y);
    static void SetResolution(int width, int height);

    // What was asked for - the ratio's two numbers, or the pixel size.
    static int WantedX();
    static int WantedY();

    // Works the whole thing out. A window of zero or less gives a 1x1 plan
    // rather than a zero one, because a render target cannot be zero across.
    static Plan PlanFor(int windowWidth, int windowHeight);

    // ======================================================================
    //  READS "free", "16:9" OR "1920x1080" - ONE PARSER, AND THAT MATTERS.
    //
    //  The settings file and the editor's custom-aspect box accept the same
    //  spellings, and a second copy of this would be two places for "what
    //  does 16:9 mean" to drift apart. The editor calls straight into it and
    //  adds only its own label.
    //
    //  The SEPARATOR decides which kind it is: a colon is a ratio, an x is a
    //  size. Nobody has to pick from a dropdown before typing.
    //
    //  Returns false with a reason somebody can act on. Does NOT change the
    //  current mode - the caller decides whether a bad value is worth
    //  obeying, which the settings loader and a text box answer differently.
    // ======================================================================
    static bool ParseSpec(std::string_view text, Mode& outMode, int& outX, int& outY,
                          std::string& outError);

    // The largest box of this shape that fits inside `room`, centred on
    // nothing - just the size. Shared by PlanFor and by the editor's panel
    // layout, which are different policies over the same three lines of
    // arithmetic.
    static Vec2 FitInside(Vec2 room, float ratio);
};

} // namespace eng
