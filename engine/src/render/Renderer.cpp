// =============================================================================
//  Renderer.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. Renderer.h is the specification; read it first.
//
//  Everything here works in SCREEN pixels with y pointing down. The game world
//  is y-up, and the single place the two are reconciled is Camera::ViewMatrix.
// =============================================================================

#include <engine/render/Renderer.h>

namespace eng {

// Borrows the drawing object the window already owns. The renderer does not
// create one of its own, which is why it needs the window to exist first.
bool Renderer::Init(Window& /*window*/) {
    return false;
}

// Lets go of the borrowed renderer.
void Renderer::Shutdown() {
}

// Is there something to draw with? Everything below quietly does nothing when
// there is not.
bool Renderer::IsValid() {
    return false;
}

// The underlying SDL renderer, for the editor's interface and the texture
// loader. void*, so no public header has to name SDL.
void* Renderer::NativeRendererHandle() {
    return nullptr;
}

// The size in pixels of whatever is currently being drawn into - the window, or
// an off-screen picture. This is what lets one camera fill either.
Vec2 Renderer::OutputSize() {
    return Vec2{};
}

// Destroys the off-screen picture this target owns.
RenderTarget::~RenderTarget() {
}

// The underlying SDL texture for this off-screen picture, so the editor can
// show it inside a panel.
void* RenderTarget::NativeTexture() const {
    return nullptr;
}

// Makes the off-screen picture a different size, rebuilding it if needed. The
// editor calls this whenever a view panel is resized.
bool RenderTarget::Resize(int /*width*/, int /*height*/) {
    return false;
}

// Sends everything drawn from now on into an off-screen picture instead of the
// window. Passing nullptr goes back to the window.
void Renderer::SetRenderTarget(RenderTarget* /*target*/) {
}

// Which off-screen picture is currently being drawn into, or nullptr for the
// window.
RenderTarget* Renderer::CurrentRenderTarget() {
    return nullptr;
}

// Fills whatever is being drawn into with one colour, wiping the last picture.
void Renderer::Clear(Color /*color*/) {
}

// Shows the finished frame in the window.
void Renderer::Present() {
}

// Draws a one-pixel line between two points.
void Renderer::DrawLine(Vec2 /*a*/, Vec2 /*b*/, Color /*color*/) {
}

// Draws the outline of a rectangle.
void Renderer::DrawRect(Vec2 /*min*/, Vec2 /*max*/, Color /*color*/) {
}

// Draws a rectangle filled with one colour.
void Renderer::DrawFilledRect(Vec2 /*min*/, Vec2 /*max*/, Color /*color*/) {
}

// Draws a single pixel.
void Renderer::DrawPoint(Vec2 /*p*/, Color /*color*/) {
}

// Sets how large the built-in text is drawn.
void Renderer::SetTextScale(float /*scale*/) {
}

// The current text size multiplier.
float Renderer::TextScale() {
    return 1.0f;
}

// How tall one line of built-in text is, so callers can stack lines.
float Renderer::TextLineHeight() {
    return 0.0f;
}

// How wide one character of built-in text is. The font is fixed-width, so this
// is enough to measure any string.
float Renderer::TextCharWidth() {
    return 0.0f;
}

// Draws a line of text with the built-in font, starting at its top-left corner.
void Renderer::DrawText(Vec2 /*topLeft*/, const char* /*text*/, Color /*color*/) {
}

// Draws a picture centred on a point, at a size, turned by an angle, with its
// colours multiplied by a tint. This is the one call that puts a sprite on screen.
void Renderer::DrawSprite(const TextureRef& /*texture*/, Vec2 /*centre*/, Vec2 /*size*/,
                          float /*rotationDegrees*/, Color /*tint*/) {
}

} // namespace eng
