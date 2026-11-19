#pragma once

// ============================================================================
//  GameViewLayout.h - what shape the game is rendered at, and how big it is
//  shown.
//
//  WHY THIS IS NOT A COSMETIC SETTING. The Game view used to resize the
//  render target to whatever the panel happened to be, and the camera's
//  viewport with it. A bigger viewport at the same zoom shows MORE WORLD -
//  see render/Camera.h's ViewMatrix, where the zoom scales and the viewport
//  only centres - so dragging the panel wider changed how much of the level
//  the player could see. Every judgement about framing, about what is
//  off-screen, about where the UI sits, was being made against a window size
//  that no player will ever have.
//
//  So the choice is between two different things, and both are wanted:
//
//    FREE ASPECT   the render follows the panel. Convenient while building,
//                  and what the editor did before this existed.
//
//    AN ASPECT     the render keeps a shape - 16:9 - and fills the panel as
//                  far as that shape allows. How much world is visible still
//                  depends on the panel, but its PROPORTIONS do not.
//
//    A RESOLUTION  the render is exactly 1920x1080, whatever the panel is,
//                  and the picture is scaled to fit. This is the only one
//                  that answers "what will a player actually see".
//
//  ==========================================================================
//  THE SCALE IS SEPARATE FROM THE RESOLUTION, and conflating them was the
//  trap worth avoiding.
//
//  Scaling the RENDER would change what the camera sees, which is the problem
//  this feature exists to solve. So the scale only ever changes how big the
//  finished picture is DRAWN. At 1.0 one render pixel is one screen pixel -
//  which is the only honest way to judge pixel art - and a 1920x1080 target
//  in a 600-pixel panel is then mostly off the edge, deliberately.
//
//  kFitScale means "as large as fits", which is the default and what somebody
//  wants almost all of the time.
//
//  ==========================================================================
//  NO STATE, NO ImGui, NO ENGINE. Everything here is a pure function of the
//  mode and the room available, so the arithmetic that decides where the
//  picture goes can be tested without a window - and the panel is left
//  holding only the widgets. The pointer translation depends on getting this
//  right: a click lands in the wrong place the moment the drawn size and the
//  render size disagree and something forgets.
// ============================================================================

#include <string>
#include <vector>

namespace editor {

// A scale of exactly this means "as large as fits the panel".
inline constexpr float kFitScale = 0.0f;

struct GameViewMode {
    enum class Kind {
        Free,         // the render follows the panel
        Aspect,       // a fixed shape, sized to the panel
        Resolution,   // an exact pixel size
    };

    Kind        kind = Kind::Free;
    int         x    = 0;   // ratio numerator, or pixel width
    int         y    = 0;   // ratio denominator, or pixel height
    std::string label;      // what the dropdown shows

    bool operator==(const GameViewMode& other) const {
        return kind == other.kind && x == other.x && y == other.y;
    }
};

// Where the picture goes, in one answer.
//
// THE TWO SIZES ARE DIFFERENT NUMBERS and that is the whole point of this
// struct: `target` is what the camera renders at, `draw` is how big it
// appears. Anything converting a mouse position has to scale between them.
struct GameViewLayout {
    int   targetWidth  = 1;
    int   targetHeight = 1;
    float drawWidth    = 1.0f;
    float drawHeight   = 1.0f;

    // How many render pixels one drawn pixel covers. The factor a mouse
    // position in the panel has to be multiplied by to land in the render.
    float TargetPerDrawX() const {
        return drawWidth > 0.0f ? static_cast<float>(targetWidth) / drawWidth : 1.0f;
    }
    float TargetPerDrawY() const {
        return drawHeight > 0.0f ? static_cast<float>(targetHeight) / drawHeight : 1.0f;
    }
};

// The built-in entries, in the order the dropdown shows them. Free Aspect is
// always first, so index 0 is the default and the dropdown opens on what the
// editor used to do.
const std::vector<GameViewMode>& BuiltInGameViewModes();

// Works out both sizes. `availWidth/Height` is the room inside the panel.
//
// A zero or negative room gives a 1x1 layout rather than a zero one: a render
// target of zero cannot be created, and the panel would then report "this
// view could not be created" every time somebody dragged it shut.
GameViewLayout LayoutGameView(const GameViewMode& mode, float availWidth,
                              float availHeight, float scale);

// Parses "16:9" or "1920x1080" into a mode, for the custom-entry box.
//
// ONE FUNCTION FOR BOTH SPELLINGS, because which one somebody meant is
// already written in the separator they typed, and asking them to pick from a
// second dropdown first would be a worse way to find that out.
//
// Returns false and fills outError with something a person can act on.
bool ParseGameViewMode(const std::string& text, GameViewMode& outMode,
                       std::string& outError);

} // namespace editor
