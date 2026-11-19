// ============================================================================
//  GameViewLayout.cpp - the arithmetic. See GameViewLayout.h for the why.
// ============================================================================

#include "GameViewLayout.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace editor {
namespace {

// Rounded to whole pixels, and never below one. A render target cannot be
// zero across, and a half pixel is not a size a texture can have.
int Pixels(float value) {
    return std::max(1, static_cast<int>(std::lround(value)));
}

bool ReadTwoNumbers(const std::string& text, char separator, int& outA, int& outB) {
    const std::size_t at = text.find(separator);
    if (at == std::string::npos) {
        return false;
    }

    const std::string left  = text.substr(0, at);
    const std::string right = text.substr(at + 1);
    if (left.empty() || right.empty()) {
        return false;
    }

    // Parsed by hand rather than with std::stoi, which throws on text and
    // would accept "16abc" as 16. A custom entry is typed by a person, so
    // both of those matter.
    auto whole = [](const std::string& part, int& out) {
        int value = 0;
        for (const char c : part) {
            if (std::isspace(static_cast<unsigned char>(c)) != 0) {
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
                return false;
            }
            value = value * 10 + (c - '0');
            if (value > 100000) {
                return false;   // beyond anything meaningful, and overflow next
            }
        }
        out = value;
        return value > 0;
    };

    return whole(left, outA) && whole(right, outB);
}

} // namespace

const std::vector<GameViewMode>& BuiltInGameViewModes() {
    // ======================================================================
    //  FREE ASPECT IS FIRST, and that is load-bearing rather than tidy:
    //  index 0 is what a project with no remembered choice opens on, so the
    //  editor behaves exactly as it did before this feature existed until
    //  somebody asks for something else.
    //
    //  The rest are the shapes and sizes worth having to hand. The aspects
    //  come before the resolutions because a shape is the commoner question
    //  while building - "does my UI survive a wide screen" - and a resolution
    //  is the one you check before shipping.
    // ======================================================================
    static const std::vector<GameViewMode> modes = {
        {GameViewMode::Kind::Free, 0, 0, "Free Aspect"},

        {GameViewMode::Kind::Aspect, 16, 9, "16:9"},
        {GameViewMode::Kind::Aspect, 16, 10, "16:10"},
        {GameViewMode::Kind::Aspect, 4, 3, "4:3"},
        {GameViewMode::Kind::Aspect, 3, 2, "3:2"},
        {GameViewMode::Kind::Aspect, 21, 9, "21:9"},
        {GameViewMode::Kind::Aspect, 1, 1, "1:1"},
        {GameViewMode::Kind::Aspect, 9, 16, "9:16 (portrait)"},

        {GameViewMode::Kind::Resolution, 1920, 1080, "1920 x 1080"},
        {GameViewMode::Kind::Resolution, 2560, 1440, "2560 x 1440"},
        {GameViewMode::Kind::Resolution, 3840, 2160, "3840 x 2160"},
        {GameViewMode::Kind::Resolution, 1280, 720, "1280 x 720"},
        {GameViewMode::Kind::Resolution, 1024, 768, "1024 x 768"},

        // THE SMALL ONES ARE NOT FILLER. This engine's world is in pixels and
        // its tiles are 32 across, so a 640x360 target is twenty tiles wide -
        // which is the kind of number a level is actually designed against.
        {GameViewMode::Kind::Resolution, 640, 360, "640 x 360"},
        {GameViewMode::Kind::Resolution, 480, 270, "480 x 270"},
    };
    return modes;
}

GameViewLayout LayoutGameView(const GameViewMode& mode, float availWidth,
                              float availHeight, float scale) {
    const float roomW = std::max(availWidth, 1.0f);
    const float roomH = std::max(availHeight, 1.0f);

    GameViewLayout out;

    // ---- what the camera renders at --------------------------------------
    switch (mode.kind) {
        case GameViewMode::Kind::Free:
            out.targetWidth  = Pixels(roomW);
            out.targetHeight = Pixels(roomH);
            break;

        case GameViewMode::Kind::Aspect: {
            if (mode.x <= 0 || mode.y <= 0) {
                out.targetWidth  = Pixels(roomW);
                out.targetHeight = Pixels(roomH);
                break;
            }
            // THE BIGGEST BOX OF THAT SHAPE THAT FITS. The render follows the
            // panel in SIZE, as Free Aspect does, and only its proportions
            // are pinned - which is what distinguishes an aspect from a
            // resolution and is the distinction Unity draws too.
            const float wanted = static_cast<float>(mode.x) / static_cast<float>(mode.y);
            const float room   = roomW / roomH;
            if (room > wanted) {
                out.targetHeight = Pixels(roomH);
                out.targetWidth  = Pixels(roomH * wanted);
            } else {
                out.targetWidth  = Pixels(roomW);
                out.targetHeight = Pixels(roomW / wanted);
            }
            break;
        }

        case GameViewMode::Kind::Resolution:
            // EXACTLY WHAT WAS ASKED FOR, whatever the panel is. The picture
            // is scaled to fit below; the render is not.
            out.targetWidth  = std::max(mode.x, 1);
            out.targetHeight = std::max(mode.y, 1);
            break;
    }

    // ---- and how big it is drawn -----------------------------------------
    const float targetW = static_cast<float>(out.targetWidth);
    const float targetH = static_cast<float>(out.targetHeight);

    if (scale <= kFitScale) {
        // FIT: the largest the picture can be drawn without leaving the panel,
        // keeping its shape. Never ENLARGED past 1:1, because blowing a
        // 480x270 target up to fill a wide panel makes every judgement about
        // pixel art a lie - what is wanted there is a scale of 2 or 3, chosen
        // deliberately.
        const float fit = std::min(roomW / targetW, roomH / targetH);
        const float use = std::min(fit, 1.0f);
        out.drawWidth   = targetW * use;
        out.drawHeight  = targetH * use;
    } else {
        out.drawWidth  = targetW * scale;
        out.drawHeight = targetH * scale;
    }

    out.drawWidth  = std::max(out.drawWidth, 1.0f);
    out.drawHeight = std::max(out.drawHeight, 1.0f);
    return out;
}

bool ParseGameViewMode(const std::string& text, GameViewMode& outMode,
                       std::string& outError) {
    int a = 0;
    int b = 0;

    // THE SEPARATOR SAYS WHICH ONE IT IS. A colon is a ratio, an x is a size,
    // and nobody has to pick from a dropdown before typing.
    if (ReadTwoNumbers(text, ':', a, b)) {
        outMode.kind  = GameViewMode::Kind::Aspect;
        outMode.x     = a;
        outMode.y     = b;
        outMode.label = std::to_string(a) + ":" + std::to_string(b);
        return true;
    }
    if (ReadTwoNumbers(text, 'x', a, b) || ReadTwoNumbers(text, 'X', a, b) ||
        ReadTwoNumbers(text, '*', a, b)) {
        outMode.kind  = GameViewMode::Kind::Resolution;
        outMode.x     = a;
        outMode.y     = b;
        outMode.label = std::to_string(a) + " x " + std::to_string(b);
        return true;
    }

    outError = "type a ratio like 16:9, or a size like 1920x1080";
    return false;
}

} // namespace editor
