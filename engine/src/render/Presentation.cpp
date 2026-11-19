// ============================================================================
//  Presentation.cpp - the arithmetic and the one parser. See Presentation.h.
// ============================================================================

#include <engine/render/Presentation.h>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace eng {
namespace {

struct State {
    Presentation::Mode mode = Presentation::Mode::Free;
    int                x    = 0;
    int                y    = 0;
};

// A function-local static, initialised on first use rather than at some point
// during static initialisation that nothing here controls - the same
// arrangement core/WorldUnit.cpp makes, and for the same reason.
State& Current() {
    static State state;
    return state;
}

int Pixels(float value) {
    return std::max(1, static_cast<int>(std::lround(value)));
}

// Reads a pair of whole numbers around `separator`. Parsed by hand rather
// than with std::stoi, which throws on text and would read "16abc" as 16 -
// both of which matter for a value a person typed.
bool ReadPair(std::string_view text, char separator, int& outA, int& outB) {
    const std::size_t at = text.find(separator);
    if (at == std::string_view::npos) {
        return false;
    }

    auto whole = [](std::string_view part, int& out) {
        bool any  = false;
        int  value = 0;
        for (const char c : part) {
            if (std::isspace(static_cast<unsigned char>(c)) != 0) {
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
                return false;
            }
            value = value * 10 + (c - '0');
            any   = true;
            if (value > 100000) {
                return false;   // past anything meaningful, and overflow next
            }
        }
        out = value;
        return any && value > 0;
    };

    return whole(text.substr(0, at), outA) && whole(text.substr(at + 1), outB);
}

bool EqualsNoCase(std::string_view text, std::string_view other) {
    if (text.size() != other.size()) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) !=
            std::tolower(static_cast<unsigned char>(other[i]))) {
            return false;
        }
    }
    return true;
}

std::string_view Trimmed(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace

Presentation::Mode Presentation::CurrentMode() { return Current().mode; }
int                Presentation::WantedX() { return Current().x; }
int                Presentation::WantedY() { return Current().y; }

void Presentation::SetFree() {
    Current() = State{};
}

void Presentation::SetAspect(int x, int y) {
    if (x <= 0 || y <= 0) {
        return;   // refused rather than clamped; see the header
    }
    Current() = State{Mode::Aspect, x, y};
}

void Presentation::SetResolution(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    Current() = State{Mode::Resolution, width, height};
}

Vec2 Presentation::FitInside(Vec2 room, float ratio) {
    const float w = std::max(room.x, 1.0f);
    const float h = std::max(room.y, 1.0f);
    if (ratio <= 0.0f) {
        return Vec2{w, h};
    }

    // Limited by whichever side runs out first.
    if (w / h > ratio) {
        return Vec2{h * ratio, h};
    }
    return Vec2{w, w / ratio};
}

Presentation::Plan Presentation::PlanFor(int windowWidth, int windowHeight) {
    const float roomW = static_cast<float>(std::max(windowWidth, 1));
    const float roomH = static_cast<float>(std::max(windowHeight, 1));

    const State& state = Current();

    Plan plan;

    if (state.mode == Mode::Free) {
        // THE GAME FILLS THE WINDOW and there is nothing behind it. The same
        // path the engine took before any of this existed, so a project that
        // never asks for a shape pays nothing for the feature.
        plan.renderWidth  = Pixels(roomW);
        plan.renderHeight = Pixels(roomH);
        plan.width        = roomW;
        plan.height       = roomH;
        plan.letterboxed  = false;
        return plan;
    }

    const float ratio =
        static_cast<float>(state.x) / static_cast<float>(std::max(state.y, 1));

    // ---- what the camera renders at --------------------------------------
    if (state.mode == Mode::Resolution) {
        // EXACTLY WHAT WAS ASKED FOR, whatever the window is. This is the
        // mode that makes pixel art land on the same grid everywhere.
        plan.renderWidth  = std::max(state.x, 1);
        plan.renderHeight = std::max(state.y, 1);
    } else {
        // AS LARGE AS THE WINDOW ALLOWS AT THAT SHAPE. A bigger monitor gets
        // a bigger picture of the same framing, which is the distinction
        // between pinning a shape and pinning a size.
        const Vec2 fitted = FitInside(Vec2{roomW, roomH}, ratio);
        plan.renderWidth  = Pixels(fitted.x);
        plan.renderHeight = Pixels(fitted.y);
    }

    // ---- and where it lands ----------------------------------------------
    //
    // SCALED UP TO FILL, unlike the editor's panel, which stops at 1:1. A
    // player wants the game as large as the screen allows; a developer
    // looking at pixel art wants to know what one pixel is. Same arithmetic,
    // different policy, and this is the one place that difference lives.
    const Vec2 shown = FitInside(Vec2{roomW, roomH}, ratio);
    plan.width       = shown.x;
    plan.height      = shown.y;
    plan.x           = (roomW - shown.x) * 0.5f;
    plan.y           = (roomH - shown.y) * 0.5f;

    // A HALF PIXEL COUNTS AS FILLING IT. Comparing floats for equality here
    // would letterbox a window that is one rounding error off the shape, and
    // then clear the backbuffer every frame to draw nothing.
    plan.letterboxed = plan.x > 0.5f || plan.y > 0.5f;
    return plan;
}

bool Presentation::ParseSpec(std::string_view text, Mode& outMode, int& outX,
                            int& outY, std::string& outError) {
    const std::string_view trimmed = Trimmed(text);

    if (trimmed.empty() || EqualsNoCase(trimmed, "free") ||
        EqualsNoCase(trimmed, "none") || EqualsNoCase(trimmed, "window")) {
        outMode = Mode::Free;
        outX    = 0;
        outY    = 0;
        return true;
    }

    int a = 0;
    int b = 0;

    // THE SEPARATOR SAYS WHICH KIND IT IS - see the header.
    if (ReadPair(trimmed, ':', a, b)) {
        outMode = Mode::Aspect;
        outX    = a;
        outY    = b;
        return true;
    }
    if (ReadPair(trimmed, 'x', a, b) || ReadPair(trimmed, 'X', a, b) ||
        ReadPair(trimmed, '*', a, b)) {
        outMode = Mode::Resolution;
        outX    = a;
        outY    = b;
        return true;
    }

    outError = "expected \"free\", a ratio like 16:9, or a size like 1920x1080";
    return false;
}

} // namespace eng
