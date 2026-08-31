#pragma once

// ============================================================================
//  Renderer.h - the drawing layer: lines, rectangles, text and sprites.
//
//  WHY THIS IS SEPARATE FROM Window
//  A Window's job is to own an operating-system resource and manage its
//  lifetime. Drawing is a different job that happens to need a window. Bolting
//  a dozen draw functions onto Window would turn it into a class that does
//  everything, so instead this layer borrows the window's renderer and does
//  the drawing.
//
//  COORDINATES HERE ARE SCREEN PIXELS, WITH Y POINTING DOWN
//  That is what the graphics hardware works in. The game world is measured
//  differently - y points UP, like in a maths lesson - and the single place
//  the two are reconciled is Camera::ViewMatrix. Nothing above this layer sees
//  screen coordinates unless it deliberately asked for them.
// ============================================================================

#include <engine/math/Vec2.h>
#include <engine/platform/SdlHandles.h>
#include <engine/render/Texture.h>

namespace eng {

class Window;

// A colour, one byte per channel, 0-255. `unsigned char` is used because that
// type holds exactly 0 to 255, which is the range a colour channel has.
struct Color {
    unsigned char r = 255, g = 255, b = 255, a = 255;   // a = alpha (opacity)

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

    constexpr Color WithAlpha(unsigned char alpha) const { return Color{r, g, b, alpha}; }

    friend constexpr bool operator==(const Color&, const Color&) = default;
};

// A picture that can be drawn INTO instead of drawn onto the window.
//
// This is what makes the editor's Scene view and Game view possible at the
// same time. Each view renders the world into its own RenderTarget, and the
// editor then displays those two pictures inside two panels. Drawing straight
// to the window could only ever produce one view.
class RenderTarget {
public:
    RenderTarget() = default;
    ~RenderTarget();

    RenderTarget(const RenderTarget&)            = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    // Makes the target the requested size, doing nothing if it already is.
    //
    // That check matters: a docked panel reports a slightly different size on
    // almost every frame while it is being dragged, and rebuilding the picture
    // every frame is a stutter you can see.
    bool Resize(int width, int height);

    int  Width()   const { return m_width; }
    int  Height()  const { return m_height; }
    bool IsValid() const { return m_texture != nullptr; }

    // Handed to the GUI so it can display this picture inside a panel.
    // void* for the same reason as everywhere else in this layer: naming the
    // SDL type here would put SDL back into a public header.
    void* NativeTexture() const;

private:
    friend class Renderer;

    TexturePtr m_texture;   // a unique_ptr; see SdlHandles.h
    int        m_width  = 0;
    int        m_height = 0;
};

class Renderer {
public:
    static bool Init(Window& window);
    static void Shutdown();
    static bool IsValid();

    // The size of whatever is currently being drawn into, in pixels - the
    // window, or a RenderTarget if one is bound.
    static Vec2 OutputSize();

    static void Clear(Color color);
    static void Present();

    // Sends everything drawn from now on into `target`, or back to the window
    // when given nullptr.
    //
    // Everything must be back on the window before the editor's GUI is drawn,
    // or the whole interface would end up inside whichever panel's picture
    // happened to be bound last.
    static void          SetRenderTarget(RenderTarget* target);
    static RenderTarget* CurrentRenderTarget();

    // ---- shapes, in screen pixels ----------------------------------------
    static void DrawLine(Vec2 a, Vec2 b, Color color);
    static void DrawRect(Vec2 min, Vec2 max, Color color);         // outline only
    static void DrawFilledRect(Vec2 min, Vec2 max, Color color);
    static void DrawPoint(Vec2 p, Color color);

    // ---- text -------------------------------------------------------------
    // Uses the small 8x8 font built into SDL3. It is plain, but it needs no
    // font file and no extra library, which makes it exactly right for an
    // on-screen score or timer.
    static void  DrawText(Vec2 topLeft, const char* text, Color color);
    static float TextLineHeight();
    static float TextCharWidth();

    // The built-in font is 8 pixels tall, which is hard to read on a modern
    // display. This multiplies it up.
    static void  SetTextScale(float scale);
    static float TextScale();

    // ---- sprites ----------------------------------------------------------
    // Draws a texture centred on `centre`, `size` pixels across, turned by
    // `rotationDegrees` clockwise (which is the direction SDL rotates).
    // `tint` multiplies the image's colours, so White() draws it unchanged.
    static void DrawSprite(const TextureRef& texture, Vec2 centre, Vec2 size,
                           float rotationDegrees, Color tint);

    // Engine-internal: the GUI layer and the texture loader both need the
    // underlying SDL renderer. void*, as everywhere else in this layer.
    static void* NativeRendererHandle();
};

} // namespace eng
