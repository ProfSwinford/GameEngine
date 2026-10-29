#pragma once

// ============================================================================
//  Font.h - a TrueType font, baked once into one texture.
//
//  Before this existed the engine could draw exactly one kind of text: SDL's
//  built-in 8x8 debug font, at whatever size a whole-number scale happened to
//  give. That is right for a frame counter and wrong for anything a player is
//  meant to read, so a UI object can be given a .ttf of its own.
//
//  ==========================================================================
//  HOW IT WORKS, in one paragraph.
//
//  A .ttf file describes letter SHAPES as curves. Drawing those curves every
//  frame would be slow and complicated, so the file is read once and every
//  character is drawn into a single image - an ATLAS - at one particular
//  size. Drawing text afterwards is then just copying rectangles out of that
//  image, which is the same thing the sprite renderer already does all day.
//
//  THE SIZE IS BAKED IN. An atlas made at 16 pixels tall holds 16-pixel
//  letters, and asking for the same font at 32 makes a SECOND atlas. That is
//  why LoadFont takes a size as well as a path, and why the cache is keyed on
//  both: scaling a 16-pixel atlas up to 32 looks exactly as blurry as it
//  sounds.
//
//  ==========================================================================
//  ASCII 32 TO 126, and no further.
//
//  Space to tilde: the characters on a keyboard. A full Unicode atlas is a
//  different kind of program - it needs glyphs baked on demand, an atlas that
//  grows, and a cache that can evict - and none of that earns its place in an
//  engine for learning. Anything outside the range draws as nothing, rather
//  than as a wrong letter.
// ============================================================================

#include <engine/math/Vec2.h>
#include <engine/render/Texture.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>

namespace eng {

// Where one character lives in the atlas, and how to place it.
struct Glyph {
    // The region of the atlas holding this character, in pixels.
    float sx = 0.0f;
    float sy = 0.0f;
    float sw = 0.0f;
    float sh = 0.0f;

    // Where to draw it relative to the pen. A 'y' hangs below the line and a
    // 'T' does not, and this is the difference.
    Vec2 offset{};

    // How far the pen moves afterwards. NOT the same as the width: letters
    // are spaced by design, not by how wide their ink happens to be.
    float advance = 0.0f;
};

inline constexpr int kFirstGlyph = 32;    // space
inline constexpr int kLastGlyph  = 126;   // ~
inline constexpr int kGlyphCount = kLastGlyph - kFirstGlyph + 1;

// ---------------------------------------------------------------------------
//  Splitting text into lines, in ONE place.
//
//  Every piece of code that draws or measures text has to agree about where
//  the lines are, and in particular about \r\n. Text pasted from a Windows
//  editor arrives with carriage returns, and a \r that reaches a draw call
//  is either a stray glyph or a line that silently does not break. Having
//  each caller strip it itself is how one of them ends up not doing it.
//
//  `fn` is called once per line, with the \r and \n already removed.
// ---------------------------------------------------------------------------
template <typename Fn>
void ForEachLine(std::string_view text, Fn&& fn) {
    std::size_t start = 0;
    while (true) {
        const std::size_t breakAt = text.find('\n', start);
        std::string_view  line    = (breakAt == std::string_view::npos)
                                        ? text.substr(start)
                                        : text.substr(start, breakAt - start);

        // A \r immediately before the \n is part of the break, not content.
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        fn(line);

        if (breakAt == std::string_view::npos) {
            return;
        }
        start = breakAt + 1;
    }
}

struct Font {
    std::string path;
    float       pixelHeight = 0.0f;

    // The vertical metrics the font itself specifies, already scaled to
    // pixelHeight. ascent is above the baseline and descent below it (and is
    // negative, as the format defines it).
    float ascent  = 0.0f;
    float descent = 0.0f;
    float lineGap = 0.0f;

    TextureRef                     atlas;
    std::array<Glyph, kGlyphCount> glyphs{};

    // True when the .ttf could not be read and this is the stand-in. Text
    // still draws - through the built-in 8x8 font - so a missing font file
    // never means an invisible interface, which would be far harder to
    // diagnose than ugly text.
    bool isFallback = false;

    // Baseline to baseline.
    float LineHeight() const;

    // How big this text would be, in pixels. Needed for centring, for
    // right-alignment, and for a button that sizes itself to its label.
    Vec2 Measure(std::string_view text) const;
};

using FontRef = std::shared_ptr<Font>;

} // namespace eng
