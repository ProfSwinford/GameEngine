#pragma once

// =============================================================================
//  WEEK 6 - the draw layer.
//
//  Week 1's Window could clear and present. Week 6 needed lines, boxes,
//  circles, text and sprites, and there were two ways to get them:
//
//    (a) put a dozen more methods on Window, or
//    (b) a small layer that borrows the window's native renderer.
//
//  (b), because Window is the PLATFORM object - it owns an OS resource and a
//  lifetime - and drawing is a separate concern that happens to need it. A
//  Window with twenty draw methods is a god object, and by Week 10 it would
//  have had thirty.
//
//  Everything here is in SCREEN space, in pixels, with the origin at the top
//  left and y pointing DOWN, because that is what the platform gives us. World
//  space is y-up and the conversion happens exactly once, in Camera. Callers
//  above this layer never see screen coordinates unless they asked for them.
//
//  Colour lives here rather than in DebugDraw because the sprite renderer and
//  the debug renderer both need it and neither should include the other.
// =============================================================================

#include <engine/core/Types.h>
#include <engine/math/Vec2.h>
#include <engine/platform/SdlHandles.h>
#include <engine/resource/Handle.h>

namespace eng {

class Window;
struct Texture;

struct Color {
    u8 r = 255, g = 255, b = 255, a = 255;

    static constexpr Color White()   { return {255, 255, 255, 255}; }
    static constexpr Color Black()   { return {  0,   0,   0, 255}; }
    static constexpr Color Red()     { return {235,  64,  52, 255}; }
    static constexpr Color Green()   { return { 76, 205,  86, 255}; }
    static constexpr Color Blue()    { return { 66, 135, 245, 255}; }
    static constexpr Color Yellow()  { return {245, 205,  66, 255}; }
    static constexpr Color Cyan()    { return { 66, 233, 245, 255}; }
    static constexpr Color Magenta() { return {245,  66, 233, 255}; }
    static constexpr Color Orange()  { return {245, 145,  66, 255}; }
    static constexpr Color Grey()    { return {128, 128, 128, 255}; }

    constexpr Color WithAlpha(u8 alpha) const { return Color{r, g, b, alpha}; }

    friend constexpr bool operator==(const Color&, const Color&) = default;
};

// An off-screen surface the world can be rendered into, so the result can be
// displayed inside a panel rather than filling the window.
//
// This is what makes a Unity-style Scene view and Game view possible at all:
// two views of the same world, at different sizes, through different cameras,
// both on screen at once. Rendering straight to the back buffer can only ever
// produce one.
class RenderTarget {
public:
    RenderTarget() = default;
    ~RenderTarget();

    RenderTarget(const RenderTarget&)            = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    // Recreates the texture only when the size actually changes, because a
    // docked panel reports a new size on almost every frame it is being
    // dragged, and reallocating a render texture per frame is a stutter you can
    // see. Returns false if the texture could not be created.
    bool Resize(i32 width, i32 height);

    i32  Width() const  { return m_width; }
    i32  Height() const { return m_height; }
    bool IsValid() const { return m_texture != nullptr; }

    // For ImGui::Image. void* for the same reason as everywhere else in this
    // layer - naming the SDL type here would put it back in a public header.
    void* NativeTexture() const;

private:
    friend class Renderer;

    TexturePtr m_texture;
    i32        m_width  = 0;
    i32        m_height = 0;
};

class Renderer {
public:
    static bool Init(Window& window);
    static void Shutdown();
    static bool IsValid();

    // The window's current drawable size in pixels. The camera needs it every
    // frame and it can change under a resize.
    static Vec2 OutputSize();

    static void Clear(Color color);
    static void Present();

    // Directs subsequent drawing into a RenderTarget, or back to the window
    // when passed nullptr. **Everything must be back on the window before the
    // GUI is rendered**, or the IDE would draw into whichever panel's texture
    // happened to be bound last.
    static void SetRenderTarget(RenderTarget* target);
    static RenderTarget* CurrentRenderTarget();

    // --- screen-space primitives ------------------------------------------
    static void DrawLine(Vec2 a, Vec2 b, Color color);
    static void DrawRect(Vec2 min, Vec2 max, Color color);        // outline
    static void DrawFilledRect(Vec2 min, Vec2 max, Color color);
    static void DrawPoint(Vec2 p, Color color);

    // SDL3's built-in 8x8 bitmap font. Deliberately ugly, requires no font
    // file, no SDL_ttf, and no asset pipeline - exactly right for a HUD, and
    // the reason Week 6 says not to spend the week on typography.
    static void DrawDebugText(Vec2 topLeft, const char* text, Color color);
    static f32  DebugTextLineHeight();
    static f32  DebugTextCharWidth();

    // Scales the debug font. SDL's debug text is 8 pixels tall, which is
    // unreadable on a high-DPI display; this multiplies it. Blurry at
    // non-integer scales, which is expected and fine for a HUD.
    static void SetDebugTextScale(f32 scale);
    static f32  DebugTextScale();

    // --- sprites -----------------------------------------------------------
    // `centre` is where the sprite's middle lands on screen, `size` is its
    // full extent in pixels, `rotationDegrees` is CLOCKWISE (SDL's
    // convention). The caller converts from world space; see SpriteComponent.
    static void DrawSprite(Handle<Texture> texture, Vec2 centre, Vec2 size,
                           f32 rotationDegrees, Color tint);

    // Engine-internal: the ImGui renderer backend and the texture loader both
    // need it. void* for the same reason as Window's - see that header.
    static void* NativeRendererHandle();
};

} // namespace eng
