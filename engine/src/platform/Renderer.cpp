// WEEK 6 - the draw layer. See Renderer.h.
//
// Screen space, pixels, y down. The world-to-screen conversion happens in
// Camera and nowhere else.

#include <engine/core/Log.h>
#include <engine/platform/Renderer.h>
#include <engine/platform/Window.h>
#include <engine/resource/ResourceManager.h>

#include <SDL3/SDL.h>

#include <algorithm>

namespace eng {
namespace {

SDL_Renderer* g_renderer = nullptr;
f32           g_textScale = 2.0f;

// Borrowed, never owned. Present() delegates to it rather than calling
// SDL_RenderPresent a second time: Window::Present is the Week 1 API and
// having two functions in the engine that both present the frame is exactly
// the duplication this layer was split out to avoid. It also keeps
// Window::Present LIVE - the Ch. 2.2 linker exercise in
// docs/week01-notes.md only works on a function something actually calls.
Window* g_window = nullptr;

// The target currently bound. Null means the window's back buffer.
RenderTarget* g_target = nullptr;

void ApplyColor(Color c) {
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, c.r, c.g, c.b, c.a);
}

} // namespace

bool Renderer::Init(Window& window) {
    if (!window.IsValid()) {
        ENGINE_LOG_ERROR(Channels::kRender, "Renderer::Init called with an invalid window");
        return false;
    }
    g_window   = &window;
    g_renderer = static_cast<SDL_Renderer*>(window.NativeRendererHandle());
    ENGINE_LOG_INFO(Channels::kRender, "Renderer up ({})", SDL_GetRendererName(g_renderer));
    return g_renderer != nullptr;
}

void Renderer::Shutdown() {
    // Borrowed, not owned: the Window created both and the Window destroys
    // both. Forgetting which of those two is true is how a renderer gets
    // destroyed twice.
    g_renderer = nullptr;
    g_window   = nullptr;
    ENGINE_LOG_INFO(Channels::kRender, "Renderer down");
}

bool Renderer::IsValid() {
    return g_renderer != nullptr;
}

void* Renderer::NativeRendererHandle() {
    return g_renderer;
}

Vec2 Renderer::OutputSize() {
    // Reports the CURRENT target, not the window - so a camera whose viewport
    // is set from this while a panel's target is bound gets the panel's size,
    // which is what makes the Scene and Game views frame correctly at
    // different sizes.
    if (g_renderer == nullptr) {
        return Vec2{0.0f, 0.0f};
    }
    if (g_target != nullptr && g_target->IsValid()) {
        return Vec2{static_cast<f32>(g_target->Width()),
                    static_cast<f32>(g_target->Height())};
    }
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(g_renderer, &w, &h);
    return Vec2{static_cast<f32>(w), static_cast<f32>(h)};
}

// ---------------------------------------------------------------------------
//  RenderTarget
// ---------------------------------------------------------------------------

RenderTarget::~RenderTarget() = default;

void* RenderTarget::NativeTexture() const {
    return m_texture.get();
}

bool RenderTarget::Resize(i32 width, i32 height) {
    // Clamped rather than refused. A docked panel can momentarily report a zero
    // or negative content region while it is being dragged or collapsed, and a
    // view that threw its texture away every time that happened would flicker.
    width  = std::max(width, 1);
    height = std::max(height, 1);

    if (m_texture != nullptr && width == m_width && height == m_height) {
        return true;   // the common case, every frame
    }
    if (g_renderer == nullptr) {
        return false;
    }

    m_texture.reset(SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA8888,
                                      SDL_TEXTUREACCESS_TARGET, width, height));
    if (m_texture == nullptr) {
        ENGINE_LOG_ERROR(Channels::kRender, "could not create a {}x{} render target: {}",
                         width, height, SDL_GetError());
        m_width  = 0;
        m_height = 0;
        return false;
    }

    // Nearest, for the same reason sprites use it: a Scene view at a
    // non-integer zoom should show crisp pixels rather than a blurred guess.
    SDL_SetTextureScaleMode(m_texture.get(), SDL_SCALEMODE_NEAREST);

    m_width  = width;
    m_height = height;
    return true;
}

void Renderer::SetRenderTarget(RenderTarget* target) {
    if (g_renderer == nullptr) {
        return;
    }
    SDL_Texture* texture =
        (target != nullptr && target->IsValid())
            ? static_cast<SDL_Texture*>(target->NativeTexture())
            : nullptr;

    if (!SDL_SetRenderTarget(g_renderer, texture)) {
        ENGINE_LOG_ERROR(Channels::kRender, "SDL_SetRenderTarget failed: {}", SDL_GetError());
        return;
    }
    g_target = (texture != nullptr) ? target : nullptr;
}

RenderTarget* Renderer::CurrentRenderTarget() {
    return g_target;
}

void Renderer::Clear(Color color) {
    if (g_renderer == nullptr) {
        return;
    }
    ApplyColor(color);
    SDL_RenderClear(g_renderer);
}

void Renderer::Present() {
    // Delegated to the Window rather than duplicated. See the note on g_window.
    if (g_window != nullptr) {
        g_window->Present();
    }
}

void Renderer::DrawLine(Vec2 a, Vec2 b, Color color) {
    if (g_renderer == nullptr) {
        return;
    }
    ApplyColor(color);
    SDL_RenderLine(g_renderer, a.x, a.y, b.x, b.y);
}

void Renderer::DrawRect(Vec2 min, Vec2 max, Color color) {
    if (g_renderer == nullptr) {
        return;
    }
    ApplyColor(color);
    SDL_FRect rect{min.x, min.y, max.x - min.x, max.y - min.y};
    SDL_RenderRect(g_renderer, &rect);
}

void Renderer::DrawFilledRect(Vec2 min, Vec2 max, Color color) {
    if (g_renderer == nullptr) {
        return;
    }
    ApplyColor(color);
    SDL_FRect rect{min.x, min.y, max.x - min.x, max.y - min.y};
    SDL_RenderFillRect(g_renderer, &rect);
}

void Renderer::DrawPoint(Vec2 p, Color color) {
    if (g_renderer == nullptr) {
        return;
    }
    ApplyColor(color);
    SDL_RenderPoint(g_renderer, p.x, p.y);
}

void Renderer::SetDebugTextScale(f32 scale) {
    g_textScale = (scale > 0.0f) ? scale : 1.0f;
}

f32 Renderer::DebugTextScale() {
    return g_textScale;
}

f32 Renderer::DebugTextLineHeight() {
    return static_cast<f32>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) * g_textScale + 2.0f;
}

f32 Renderer::DebugTextCharWidth() {
    return static_cast<f32>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) * g_textScale;
}

void Renderer::DrawDebugText(Vec2 topLeft, const char* text, Color color) {
    if (g_renderer == nullptr || text == nullptr) {
        return;
    }
    ApplyColor(color);

    // SDL's debug font is a hardcoded 8x8 bitmap. Scaling it up is done with
    // the render scale rather than by drawing it four times: SDL_SetRenderScale
    // multiplies everything, so the coordinates have to be divided back down.
    // Blurry at non-integer scales, which is expected and fine for a HUD.
    if (g_textScale != 1.0f) {
        float sx = 1.0f, sy = 1.0f;
        SDL_GetRenderScale(g_renderer, &sx, &sy);
        SDL_SetRenderScale(g_renderer, g_textScale, g_textScale);
        SDL_RenderDebugText(g_renderer, topLeft.x / g_textScale, topLeft.y / g_textScale,
                            text);
        SDL_SetRenderScale(g_renderer, sx, sy);
    } else {
        SDL_RenderDebugText(g_renderer, topLeft.x, topLeft.y, text);
    }
}

void Renderer::DrawSprite(Handle<Texture> handle, Vec2 centre, Vec2 size,
                          f32 rotationDegrees, Color tint) {
    if (g_renderer == nullptr) {
        return;
    }

    Texture* texture = ResourceManager::Get(handle);
    if (texture == nullptr || texture->native == nullptr) {
        return;   // stale, failed, or still loading - Get() already reported it
    }

    auto* sdlTexture = static_cast<SDL_Texture*>(texture->native);
    SDL_SetTextureColorMod(sdlTexture, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(sdlTexture, tint.a);
    SDL_SetTextureBlendMode(sdlTexture, SDL_BLENDMODE_BLEND);

    SDL_FRect dst{centre.x - size.x * 0.5f, centre.y - size.y * 0.5f, size.x, size.y};
    SDL_FPoint pivot{size.x * 0.5f, size.y * 0.5f};

    // SDL rotates CLOCKWISE for a positive angle. World rotation is
    // counter-clockwise in a y-up space, and the camera's view matrix mirrors
    // y - which turns a world CCW rotation into a screen CW one. The two flips
    // cancel into "pass the world angle through in degrees", and the
    // asymmetric marker_up.bmp in the provided scene is how that gets checked
    // rather than assumed.
    SDL_RenderTextureRotated(g_renderer, sdlTexture, nullptr, &dst,
                             static_cast<double>(rotationDegrees), &pivot,
                             SDL_FLIP_NONE);
}

} // namespace eng
