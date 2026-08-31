// ============================================================================
//  Renderer.cpp - the drawing layer declared in Renderer.h.
//
//  Everything here is in screen pixels with y pointing down, because that is
//  what SDL works in. Converting from world coordinates happens in Camera.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/platform/Window.h>
#include <engine/render/Renderer.h>

#include <SDL3/SDL.h>

#include <algorithm>

namespace eng {
namespace {

// The SDL renderer the Window created. BORROWED, never owned - the Window
// created it and the Window destroys it. Getting that backwards would destroy
// it twice.
SDL_Renderer* g_renderer = nullptr;

// Also borrowed. Present() forwards to Window::Present rather than calling
// SDL_RenderPresent itself, so there is exactly one function in the engine
// that finishes a frame.
Window* g_window = nullptr;

// Which off-screen picture is currently being drawn into. Null means the
// window itself.
RenderTarget* g_target = nullptr;

float g_textScale = 2.0f;

void ApplyColor(Color c) {
    // BLEND mode makes the alpha channel mean something: a colour with a < 255
    // is drawn see-through. Without it, alpha would be ignored.
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, c.r, c.g, c.b, c.a);
}

} // namespace

bool Renderer::Init(Window& window) {
    if (!window.IsValid()) {
        ENGINE_LOG_ERROR(Channels::kRender, "the renderer was given a window that "
                                            "failed to open");
        return false;
    }
    g_window   = &window;
    g_renderer = static_cast<SDL_Renderer*>(window.NativeRendererHandle());
    ENGINE_LOG_INFO(Channels::kRender, "renderer ready ({})",
                    SDL_GetRendererName(g_renderer));
    return g_renderer != nullptr;
}

void Renderer::Shutdown() {
    // Just forget the pointers. Destroying them here would be destroying
    // something this layer never created.
    g_renderer = nullptr;
    g_window   = nullptr;
    ENGINE_LOG_INFO(Channels::kRender, "renderer shut down");
}

bool  Renderer::IsValid()              { return g_renderer != nullptr; }
void* Renderer::NativeRendererHandle() { return g_renderer; }

Vec2 Renderer::OutputSize() {
    if (g_renderer == nullptr) {
        return Vec2{0.0f, 0.0f};
    }
    // Reports the size of whatever is currently bound, not always the window.
    // That is what lets the Scene view and the Game view - two different panel
    // sizes - each frame the world correctly.
    if (g_target != nullptr && g_target->IsValid()) {
        return Vec2{static_cast<float>(g_target->Width()),
                    static_cast<float>(g_target->Height())};
    }
    int w = 0;
    int h = 0;
    SDL_GetCurrentRenderOutputSize(g_renderer, &w, &h);
    return Vec2{static_cast<float>(w), static_cast<float>(h)};
}

// ---------------------------------------------------------------------------
//  RenderTarget - an off-screen picture the world can be drawn into
// ---------------------------------------------------------------------------

// `= default` asks the compiler to write the destructor. That is enough here,
// because the only member needing clean-up is a unique_ptr, which cleans up
// after itself.
RenderTarget::~RenderTarget() = default;

void* RenderTarget::NativeTexture() const {
    return m_texture.get();
}

bool RenderTarget::Resize(int width, int height) {
    // A panel being dragged or collapsed can briefly report a size of zero.
    // Clamping to at least 1x1 keeps the picture alive through that instead of
    // throwing it away and rebuilding it a frame later, which flickers.
    width  = std::max(width, 1);
    height = std::max(height, 1);

    if (m_texture != nullptr && width == m_width && height == m_height) {
        return true;   // already the right size - the usual case, every frame
    }
    if (g_renderer == nullptr) {
        return false;
    }

    // SDL_TEXTUREACCESS_TARGET is the flag that makes a texture something you
    // can draw INTO rather than only draw FROM.
    m_texture.reset(SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA8888,
                                      SDL_TEXTUREACCESS_TARGET, width, height));
    if (m_texture == nullptr) {
        ENGINE_LOG_ERROR(Channels::kRender, "could not create a {}x{} view: {}",
                         width, height, SDL_GetError());
        m_width  = 0;
        m_height = 0;
        return false;
    }

    // NEAREST scaling keeps pixel art crisp instead of blurring it when the
    // view is zoomed to a fraction.
    SDL_SetTextureScaleMode(m_texture.get(), SDL_SCALEMODE_NEAREST);

    m_width  = width;
    m_height = height;
    return true;
}

void Renderer::SetRenderTarget(RenderTarget* target) {
    if (g_renderer == nullptr) {
        return;
    }
    SDL_Texture* texture = (target != nullptr && target->IsValid())
                               ? static_cast<SDL_Texture*>(target->NativeTexture())
                               : nullptr;

    // Passing nullptr to SDL_SetRenderTarget means "draw to the window again".
    if (!SDL_SetRenderTarget(g_renderer, texture)) {
        ENGINE_LOG_ERROR(Channels::kRender, "could not switch drawing target: {}",
                         SDL_GetError());
        return;
    }
    g_target = (texture != nullptr) ? target : nullptr;
}

RenderTarget* Renderer::CurrentRenderTarget() {
    return g_target;
}

// ---------------------------------------------------------------------------
//  Shapes
// ---------------------------------------------------------------------------

void Renderer::Clear(Color color) {
    if (g_renderer == nullptr) { return; }
    ApplyColor(color);
    SDL_RenderClear(g_renderer);
}

void Renderer::Present() {
    if (g_window != nullptr) {
        g_window->Present();
    }
}

void Renderer::DrawLine(Vec2 a, Vec2 b, Color color) {
    if (g_renderer == nullptr) { return; }
    ApplyColor(color);
    SDL_RenderLine(g_renderer, a.x, a.y, b.x, b.y);
}

void Renderer::DrawRect(Vec2 min, Vec2 max, Color color) {
    if (g_renderer == nullptr) { return; }
    ApplyColor(color);
    // SDL_FRect is x, y, width, height - not two corners - so the size has to
    // be worked out from the two points.
    SDL_FRect rect{min.x, min.y, max.x - min.x, max.y - min.y};
    SDL_RenderRect(g_renderer, &rect);
}

void Renderer::DrawFilledRect(Vec2 min, Vec2 max, Color color) {
    if (g_renderer == nullptr) { return; }
    ApplyColor(color);
    SDL_FRect rect{min.x, min.y, max.x - min.x, max.y - min.y};
    SDL_RenderFillRect(g_renderer, &rect);
}

void Renderer::DrawPoint(Vec2 p, Color color) {
    if (g_renderer == nullptr) { return; }
    ApplyColor(color);
    SDL_RenderPoint(g_renderer, p.x, p.y);
}

// ---------------------------------------------------------------------------
//  Text
// ---------------------------------------------------------------------------

void  Renderer::SetTextScale(float scale) { g_textScale = (scale > 0.0f) ? scale : 1.0f; }
float Renderer::TextScale()               { return g_textScale; }

float Renderer::TextLineHeight() {
    return static_cast<float>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) * g_textScale + 2.0f;
}

float Renderer::TextCharWidth() {
    return static_cast<float>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) * g_textScale;
}

void Renderer::DrawText(Vec2 topLeft, const char* text, Color color) {
    if (g_renderer == nullptr || text == nullptr) { return; }
    ApplyColor(color);

    if (g_textScale != 1.0f) {
        // SDL's built-in font is a fixed 8x8 bitmap and cannot be asked for a
        // larger size. Making it bigger means turning up SDL's overall render
        // scale, drawing, then putting the scale back. Because the scale
        // multiplies everything, the position has to be divided by it first so
        // the text still lands where the caller asked.
        float sx = 1.0f;
        float sy = 1.0f;
        SDL_GetRenderScale(g_renderer, &sx, &sy);
        SDL_SetRenderScale(g_renderer, g_textScale, g_textScale);
        SDL_RenderDebugText(g_renderer, topLeft.x / g_textScale,
                            topLeft.y / g_textScale, text);
        SDL_SetRenderScale(g_renderer, sx, sy);
    } else {
        SDL_RenderDebugText(g_renderer, topLeft.x, topLeft.y, text);
    }
}

// ---------------------------------------------------------------------------
//  Sprites
// ---------------------------------------------------------------------------

void Renderer::DrawSprite(const TextureRef& texture, Vec2 centre, Vec2 size,
                          float rotationDegrees, Color tint) {
    if (g_renderer == nullptr) { return; }

    // A TextureRef can be empty (nobody assigned a sprite yet) or can point at
    // a texture whose image failed to load. Either way there is nothing to
    // draw, and the failure was already reported when the load was attempted.
    if (!texture || texture->native == nullptr) {
        return;
    }

    auto* sdlTexture = static_cast<SDL_Texture*>(texture->native);

    // "Color mod" multiplies the image's own colours by the tint, so a white
    // tint leaves the picture untouched and a red tint turns it red.
    SDL_SetTextureColorMod(sdlTexture, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(sdlTexture, tint.a);
    SDL_SetTextureBlendMode(sdlTexture, SDL_BLENDMODE_BLEND);

    // SDL wants the top-left corner and a size; the caller gave us a centre.
    SDL_FRect  dst{centre.x - size.x * 0.5f, centre.y - size.y * 0.5f, size.x, size.y};
    SDL_FPoint pivot{size.x * 0.5f, size.y * 0.5f};   // turn about the middle

    SDL_RenderTextureRotated(g_renderer, sdlTexture, nullptr, &dst,
                             static_cast<double>(rotationDegrees), &pivot,
                             SDL_FLIP_NONE);
}

} // namespace eng
