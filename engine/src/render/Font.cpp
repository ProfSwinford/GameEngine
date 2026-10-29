// ============================================================================
//  Font.cpp - reading a .ttf and baking it into one texture. See Font.h.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>
#include <engine/render/Font.h>
#include <engine/render/Renderer.h>

#include <SDL3/SDL.h>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <stb_truetype.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <cmath>
#include <vector>

namespace eng {

float Font::LineHeight() const {
    if (isFallback) {
        return Renderer::TextLineHeight();
    }
    // ascent - descent + lineGap, with descent being negative, is the
    // format's own definition of one line to the next.
    return ascent - descent + lineGap;
}

Vec2 Font::Measure(std::string_view text) const {
    // AS WIDE AS THE WIDEST LINE, AS TALL AS ALL OF THEM. Both the fallback
    // and the baked font go through the same line splitter, because they
    // used not to: the fallback measured the whole string as one line, so
    // three short lines came back as one very long one and every centred or
    // right-aligned label was positioned from a width that did not exist.
    float widest = 0.0f;
    int   lines  = 0;

    ForEachLine(text, [&](std::string_view line) {
        ++lines;
        float pen = 0.0f;

        if (isFallback) {
            pen = static_cast<float>(line.size()) * Renderer::TextCharWidth();
        } else {
            for (const char raw : line) {
                const int code = static_cast<unsigned char>(raw);
                if (code < kFirstGlyph || code > kLastGlyph) {
                    continue;
                }
                pen += glyphs[static_cast<std::size_t>(code - kFirstGlyph)].advance;
            }
        }
        widest = (pen > widest) ? pen : widest;
    });

    if (lines == 0) {
        lines = 1;   // empty text is still one line tall
    }
    return Vec2{widest, static_cast<float>(lines) * LineHeight()};
}

// ---------------------------------------------------------------------------
//  Baking
// ---------------------------------------------------------------------------
namespace {

// How big an atlas to make. Chosen from the requested size rather than fixed,
// because 95 characters at 64 pixels will not fit in the square that holds
// them comfortably at 16.
int AtlasSideFor(float pixelHeight) {
    const float needed = pixelHeight * 12.0f;   // roughly 10 rows of 10
    int         side   = 128;
    while (static_cast<float>(side) < needed && side < 2048) {
        side *= 2;
    }
    return side;
}

} // namespace

FontRef BakeFont(std::string_view virtualPath, float pixelHeight, std::string& outError) {
    FontRef font    = std::make_shared<Font>();
    font->path        = std::string(virtualPath);
    font->pixelHeight = pixelHeight;

    std::vector<unsigned char> bytes;
    if (!FileSystem::ReadFile(virtualPath, bytes, outError) || bytes.empty()) {
        font->isFallback = true;
        return font;
    }

    stbtt_fontinfo info{};
    // The last argument is the index of the face inside the file. Nearly
    // every .ttf holds exactly one, and asking for its offset rather than
    // assuming zero is what makes a collection file work too.
    const int offset = stbtt_GetFontOffsetForIndex(bytes.data(), 0);
    if (offset < 0 || stbtt_InitFont(&info, bytes.data(), offset) == 0) {
        outError = "this does not look like a TrueType font";
        font->isFallback = true;
        return font;
    }

    const float scale = stbtt_ScaleForPixelHeight(&info, pixelHeight);

    int ascent  = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    font->ascent  = static_cast<float>(ascent) * scale;
    font->descent = static_cast<float>(descent) * scale;
    font->lineGap = static_cast<float>(lineGap) * scale;

    // ---- pack every character into one bitmap ------------------------------
    //
    // stbtt_PackBegin/PackFontRange does the arranging: it finds somewhere
    // for each character and reports back where it put it. The bitmap it
    // fills is ONE BYTE per pixel - coverage, not colour - which is turned
    // into RGBA below.
    const int                  side = AtlasSideFor(pixelHeight);
    std::vector<unsigned char> coverage(static_cast<std::size_t>(side) *
                                        static_cast<std::size_t>(side), 0);

    stbtt_pack_context packer{};
    if (stbtt_PackBegin(&packer, coverage.data(), side, side, 0, 1, nullptr) == 0) {
        outError         = "could not start packing the font";
        font->isFallback = true;
        return font;
    }

    std::vector<stbtt_packedchar> packed(kGlyphCount);
    const int packedOk = stbtt_PackFontRange(&packer, bytes.data(), 0, pixelHeight,
                                             kFirstGlyph, kGlyphCount, packed.data());
    stbtt_PackEnd(&packer);

    if (packedOk == 0) {
        outError = "the characters did not fit in the atlas";
        font->isFallback = true;
        return font;
    }

    // ---- coverage -> RGBA --------------------------------------------------
    //
    // WHITE EVERYWHERE, with the coverage as the alpha. Keeping the colour
    // white is what lets one atlas draw text in any colour: the renderer
    // tints it, exactly as it tints a sprite.
    std::vector<unsigned char> rgba(coverage.size() * 4);
    for (std::size_t i = 0; i < coverage.size(); ++i) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = coverage[i];
    }

    TextureRef atlas = Renderer::CreateTextureFromPixels(rgba.data(), side, side,
                                                         /*smooth=*/true, outError);
    if (!atlas) {
        font->isFallback = true;
        return font;
    }
    atlas->path  = std::string(virtualPath);
    font->atlas  = atlas;

    for (int i = 0; i < kGlyphCount; ++i) {
        const stbtt_packedchar& p = packed[static_cast<std::size_t>(i)];
        Glyph&                  g = font->glyphs[static_cast<std::size_t>(i)];
        g.sx      = static_cast<float>(p.x0);
        g.sy      = static_cast<float>(p.y0);
        g.sw      = static_cast<float>(p.x1 - p.x0);
        g.sh      = static_cast<float>(p.y1 - p.y0);
        g.offset  = Vec2{p.xoff, p.yoff};
        g.advance = p.xadvance;
    }

    outError.clear();
    return font;
}

} // namespace eng
