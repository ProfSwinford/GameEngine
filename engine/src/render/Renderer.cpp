// =============================================================================
//  Renderer.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. Renderer.h is the specification; read it first.
//
//  Everything here works in SCREEN pixels with y pointing down. The game world
//  is y-up, and the single place the two are reconciled is Camera::ViewMatrix.
// =============================================================================

#include <engine/render/Renderer.h>
#include <engine/core/Log.h>
#include <engine/platform/Window.h>

#include <SDL3/SDL.h>

#include <algorithm>


namespace eng {
namespace {

SDL_Renderer* g_renderer = nullptr;
Window* g_window = nullptr;
RenderTarget* g_target = nullptr;

float g_textScale = 2.0f;

void ApplyColor(Color c) {
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, c.r, c.g, c.b, c.a);
}

} // namespace

// Borrows the drawing object the window already owns. The renderer does not
// create one of its own, which is why it needs the window to exist first.
bool Renderer::Init(Window& window) {
    if (!window.IsValid()) {
        ENGINE_LOG_ERROR(Channels::kRender, "the renderer was given a window that failed to open.");
        return false;    
    }

    g_window = &window;
    g_renderer = static_cast<SDL_Renderer*>(window.NativeRendererHandle());
    ENGINE_LOG_INFO(Channels::kRender, "renderer ready ({})", SDL_GetRendererName(g_renderer));

    return IsValid();
}

// Lets go of the borrowed renderer.
void Renderer::Shutdown() {
    g_renderer = nullptr;
    g_window = nullptr;
    ENGINE_LOG_INFO(Channels::kRender, "renderer shutdown complete");
}

// Is there something to draw with? Everything below quietly does nothing when
// there is not.
bool Renderer::IsValid() { return g_renderer; }

// The underlying SDL renderer, for the editor's interface and the texture
// loader. void*, so no public header has to name SDL.
void* Renderer::NativeRendererHandle() { return g_renderer; }

// The size in pixels of whatever is currently being drawn into - the window, or
// an off-screen picture. This is what lets one camera fill either.
Vec2 Renderer::OutputSize() {
    
    if (g_renderer == nullptr) { return Vec2(0.0f, 0.0f); }
    
    if (g_target != nullptr && g_target->IsValid()) {
        return Vec2{static_cast<float>(g_target->Width()), 
                    static_cast<float>(g_target->Height())};
    }
    int w, h = 0;
    SDL_GetCurrentRenderOutputSize(g_renderer, &w, &h);
    return Vec2{static_cast<float>(w), static_cast<float>(h)};
}

// Destroys the off-screen picture this target owns.
RenderTarget::~RenderTarget() = default;

// The underlying SDL texture for this off-screen picture, so the editor can
// show it inside a panel.
void* RenderTarget::NativeTexture() const { return m_texture.get(); }

// Makes the off-screen picture a different size, rebuilding it if needed. The
// editor calls this whenever a view panel is resized.
bool RenderTarget::Resize(int width, int height) {
    width = std::max(width, 1);
    height = std::max(height, 1);

    if (m_texture != nullptr && width == m_width && height == m_height) {
        return true;
    }
    if (g_renderer == nullptr) {
        return false;
    }

    m_texture.reset(SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA8888,
                                      SDL_TEXTUREACCESS_TARGET, width, height));

    if (m_texture == nullptr) {
        ENGINE_LOG_ERROR(Channels::kRender, "could not create a {}x{} view: {}", width, height,
                         SDL_GetError());
        m_width = 0;
        m_height = 0;
        return false;
    }

    SDL_SetTextureScaleMode(m_texture.get(), SDL_SCALEMODE_NEAREST);

    m_width = width;
    m_height = height;
    return true;
}

// Sends everything drawn from now on into an off-screen picture instead of the
// window. Passing nullptr goes back to the window.
void Renderer::SetRenderTarget(RenderTarget* target) {
    if (g_renderer == nullptr)
        return;

    SDL_Texture* texture = (target != nullptr && target->IsValid())
                               ? static_cast<SDL_Texture*>(target->NativeTexture())
                               : nullptr;
    
    if (!SDL_SetRenderTarget(g_renderer, texture)) {
        ENGINE_LOG_ERROR(Channels::kRender, "could not switch drawing target: {}", SDL_GetError());
        return;
    }
    
    g_target = (texture != nullptr) ? target : nullptr;
}

// Which off-screen picture is currently being drawn into, or nullptr for the
// window.
RenderTarget* Renderer::CurrentRenderTarget() { return g_target; }

// Fills whatever is being drawn into with one colour, wiping the last picture.
void Renderer::Clear(Color color) {
    if (g_renderer == nullptr) { return; }

    ApplyColor(color);
    SDL_RenderClear(g_renderer);
}

// Shows the finished frame in the window.
void Renderer::Present() {
    if (g_window != nullptr)
        g_window->Present();
}

// Draws a one-pixel line between two points.
void Renderer::DrawLine(Vec2 a, Vec2 b, Color color) {
    if (g_renderer == nullptr) { return; }
    ApplyColor(color);
    SDL_RenderLine(g_renderer, a.x, a.y, b.x, b.y);
}

// Draws the outline of a rectangle.
void Renderer::DrawRect(Vec2 min, Vec2 max, Color color) {
    if (g_renderer == nullptr) { return; }

    ApplyColor(color);
    SDL_FRect rect{min.x, min.y, max.x - min.x, max.y - min.y};
    SDL_RenderRect(g_renderer, &rect);

}

// Draws a rectangle filled with one colour.
void Renderer::DrawFilledRect(Vec2 min, Vec2 max, Color color) {
    if (g_renderer == nullptr) { return; }
    
    ApplyColor(color);
    SDL_FRect rect{min.x, min.y, max.x - min.x, max.y - min.y};
    SDL_RenderFillRect(g_renderer, &rect);
}

// Draws a single pixel.
void Renderer::DrawPoint(Vec2 p, Color color) {
    if (g_renderer == nullptr) { return; }

    ApplyColor(color);
    SDL_RenderPoint(g_renderer, p.x, p.y);

}

// Sets how large the built-in text is drawn.
void Renderer::SetTextScale(float scale) { g_textScale = (scale > 0.0f) ? scale : 1.0f; }

// The current text size multiplier.
float Renderer::TextScale() { return g_textScale; }

// How tall one line of built-in text is, so callers can stack lines.
float Renderer::TextLineHeight() {
    return static_cast<float>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) * g_textScale + 2.0f;
}

// How wide one character of built-in text is. The font is fixed-width, so this
// is enough to measure any string.
float Renderer::TextCharWidth() {
    return static_cast<float>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) * g_textScale;
}

// Draws a line of text with the built-in font, starting at its top-left corner.
void Renderer::DrawText(Vec2 topLeft, const char* text, Color color) {
}

// Draws a picture centred on a point, at a size, turned by an angle, with its
// colours multiplied by a tint. This is the one call that puts a sprite on screen.
void Renderer::DrawSprite(const TextureRef& texture, Vec2 centre, Vec2 size,
                          float rotationDegrees, Color tint) {
}

} // namespace eng
