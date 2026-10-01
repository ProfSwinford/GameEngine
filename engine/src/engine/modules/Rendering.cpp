// ============================================================================
//  Rendering.cpp - textures, the camera, the drawing calls, and the debug gizmos.
//  See Rendering.h for what each piece is for.
// ============================================================================

#include <engine/modules/Diagnostics.h>
#include <engine/modules/Platform.h>
#include <engine/modules/Rendering.h>
#include <engine/modules/Settings.h>

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// ============================================================================
//  Camera.cpp - the camera declared in Camera.h.
//
//  The whole class is three or four lines of real work, because building the
//  camera as a matrix hands the rest of the job to Mat3.
// ============================================================================



namespace eng {

void Camera::SetZoom(float zoom) {
    // std::clamp keeps the value inside a range. The lower bound stops the
    // view matrix from becoming impossible to invert; the upper bound stops a
    // stray scroll wheel from zooming so far that positions lose precision.
    m_zoom = std::clamp(zoom, 0.01f, 1000.0f);
}

void Camera::Reset() {
    m_position = Vec2{0.0f, 0.0f};
    m_zoom     = 1.0f;
}

Mat3 Camera::ViewMatrix() const {
    const Vec2 half{m_viewport.x * 0.5f, m_viewport.y * 0.5f};

    // Read left to right, this says exactly what it does:
    //   1. slide the world so the camera's position sits at the origin
    //   2. scale by the zoom, and FLIP Y (that is the -m_zoom)
    //   3. slide the origin to the middle of the picture
    //
    // The negative y is the single y flip in the engine. See Camera.h.
    return Mat3::Translation(-m_position) *
           Mat3::Scaling(Vec2{m_zoom, -m_zoom}) *
           Mat3::Translation(half);
}

Mat3 Camera::InverseViewMatrix() const {
    return ViewMatrix().Inverse();
}

Vec2 Camera::WorldToScreen(Vec2 world) const {
    return ViewMatrix().TransformPoint(world);
}

Vec2 Camera::ScreenToWorld(Vec2 screen) const {
    // This is what makes clicking on things work. Because the camera is a
    // matrix, "undo the camera" is literally the inverse matrix - there is no
    // second piece of code that has to be kept in step with ViewMatrix.
    return InverseViewMatrix().TransformPoint(screen);
}

Vec2 Camera::WorldToScreenVector(Vec2 world) const {
    // TransformVector rather than TransformPoint: a size or a direction should
    // not be shifted by where the camera happens to be looking.
    return ViewMatrix().TransformVector(world);
}

AABB Camera::VisibleBounds() const {
    // Push all four corners of the screen back out into the world and take the
    // box around them.
    //
    // Doing it this way rather than "viewport divided by zoom" costs nothing
    // and keeps working unchanged on the day the camera learns to rotate.
    const Mat3 inverse = InverseViewMatrix();

    const Vec2 corners[4] = {
        inverse.TransformPoint(Vec2{0.0f, 0.0f}),
        inverse.TransformPoint(Vec2{m_viewport.x, 0.0f}),
        inverse.TransformPoint(Vec2{0.0f, m_viewport.y}),
        inverse.TransformPoint(Vec2{m_viewport.x, m_viewport.y}),
    };

    AABB bounds{corners[0], corners[0]};
    for (int i = 1; i < 4; ++i) {
        bounds.Encapsulate(corners[i]);
    }
    return bounds;
}

} // namespace eng


// ============================================================================
//  Renderer.cpp - the drawing layer declared in Renderer.h.
//
//  Everything here is in screen pixels with y pointing down, because that is
//  what SDL works in. Converting from world coordinates happens in Camera.
// ============================================================================




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


// ============================================================================
//  Gizmos.cpp - the helper shapes declared in Gizmos.h.
//
//  The design is a QUEUE. Calling Gizmos::Circle does not draw anything; it
//  adds a description of a circle to a list. Render() walks that list and
//  draws it, and EndFrame() removes the entries whose lifetime has run out.
//
//  That separation is what allows a shape to last several seconds, and what
//  allows the same list to be drawn into two different views in one frame.
// ============================================================================



namespace eng {
namespace {

enum class Shape { Line, Box, FilledBox, Circle, Text };

// One queued shape. Which fields mean what depends on `shape`; the comments
// on `a` and `b` list the four cases.
struct GizmoCommand {
    Vec2          a{};                // start / bottom-left / centre / position
    Vec2          b{};                // end   / top-right   / radius in b.x
    float         remaining = 0.0f;   // seconds left; <= 0 means "this frame only"
    Color         color{};
    std::string   text;               // only used by Shape::Text
    Shape         shape    = Shape::Line;
    GizmoSpace    space    = GizmoSpace::World;
    GizmoCategory category = GizmoCategory::Default;
};

std::vector<GizmoCommand> g_commands;

bool g_enabled = true;
bool g_categoryEnabled[static_cast<int>(GizmoCategory::Count)] = {true, true, true, true, true};
int  g_circleSegments = 24;

GizmoCommand& Enqueue() {
    // Asking for room once, up front. clear() in EndFrame keeps that memory,
    // so after the first busy frame this stops asking the system for more.
    if (g_commands.capacity() == 0) {
        g_commands.reserve(1024);
    }
    // emplace_back builds the new element directly inside the vector rather
    // than building one and then copying it in.
    g_commands.emplace_back();
    return g_commands.back();
}

bool Visible(const GizmoCommand& command) {
    if (!g_enabled) {
        return false;
    }
    const int slot = static_cast<int>(command.category);
    return slot < 0 || slot >= static_cast<int>(GizmoCategory::Count) ||
           g_categoryEnabled[slot];
}

// A world shape goes through the camera; a screen shape is already in pixels.
Vec2 ToScreen(const Camera& camera, const GizmoCommand& command, Vec2 point) {
    return (command.space == GizmoSpace::World) ? camera.WorldToScreen(point) : point;
}

} // namespace

const char* ToString(GizmoCategory category) {
    switch (category) {
        case GizmoCategory::Default:   return "Default";
        case GizmoCategory::Grid:      return "Grid";
        case GizmoCategory::Axes:      return "Origin axes";
        case GizmoCategory::Bounds:    return "Bounds";
        case GizmoCategory::Colliders: return "Colliders";
        case GizmoCategory::Count:     break;
    }
    return "?";
}

void Gizmos::Line(Vec2 a, Vec2 b, Color color, float lifetimeSeconds, GizmoSpace space,
                  GizmoCategory category) {
    GizmoCommand& command = Enqueue();
    command.shape     = Shape::Line;
    command.a         = a;
    command.b         = b;
    command.color     = color;
    command.remaining = lifetimeSeconds;
    command.space     = space;
    command.category  = category;
}

void Gizmos::Box(const AABB& box, Color color, float lifetimeSeconds, GizmoSpace space,
                 GizmoCategory category) {
    GizmoCommand& command = Enqueue();
    command.shape     = Shape::Box;
    command.a         = box.min;
    command.b         = box.max;
    command.color     = color;
    command.remaining = lifetimeSeconds;
    command.space     = space;
    command.category  = category;
}

void Gizmos::FilledBox(const AABB& box, Color color, float lifetimeSeconds,
                       GizmoSpace space, GizmoCategory category) {
    GizmoCommand& command = Enqueue();
    command.shape     = Shape::FilledBox;
    command.a         = box.min;
    command.b         = box.max;
    command.color     = color;
    command.remaining = lifetimeSeconds;
    command.space     = space;
    command.category  = category;
}

void Gizmos::Circle(Vec2 centre, float radius, Color color, float lifetimeSeconds,
                    GizmoSpace space, GizmoCategory category) {
    GizmoCommand& command = Enqueue();
    command.shape     = Shape::Circle;
    command.a         = centre;
    command.b         = Vec2{radius, 0.0f};   // only x is used
    command.color     = color;
    command.remaining = lifetimeSeconds;
    command.space     = space;
    command.category  = category;
}

void Gizmos::Text(Vec2 position, const std::string& text, Color color,
                  float lifetimeSeconds, GizmoSpace space, GizmoCategory category) {
    GizmoCommand& command = Enqueue();
    command.shape     = Shape::Text;
    command.a         = position;
    command.color     = color;
    command.remaining = lifetimeSeconds;
    command.space     = space;
    command.category  = category;

    // The text is COPIED into the command, not pointed at.
    //
    // A shape with a lifetime outlives the call that made it, and very often
    // outlives the string that was passed in - which would leave this pointing
    // at text that no longer exists. std::string owning its own copy removes
    // that whole problem, which is exactly the kind of thing std::string is for.
    command.text = text;
}

void Gizmos::TransformedBox(const Mat3& worldMatrix, Vec2 halfExtents, Color color,
                            float lifetimeSeconds, GizmoCategory category) {
    // Push the four corners of the box through the caller's own transform, then
    // join them up. Because it uses the same matrix the object uses, the
    // outline turns and scales with the object automatically.
    const Vec2 corners[4] = {
        worldMatrix.TransformPoint(Vec2{-halfExtents.x, -halfExtents.y}),
        worldMatrix.TransformPoint(Vec2{ halfExtents.x, -halfExtents.y}),
        worldMatrix.TransformPoint(Vec2{ halfExtents.x,  halfExtents.y}),
        worldMatrix.TransformPoint(Vec2{-halfExtents.x,  halfExtents.y}),
    };
    for (int i = 0; i < 4; ++i) {
        // The % 4 makes the last line join back to the first corner.
        Line(corners[i], corners[(i + 1) % 4], color, lifetimeSeconds,
             GizmoSpace::World, category);
    }
}

void Gizmos::Grid(float spacing, Color color, int halfLines) {
    if (spacing <= 0.0f) {
        return;
    }
    const float extent = spacing * static_cast<float>(halfLines);
    for (int i = -halfLines; i <= halfLines; ++i) {
        const float offset = spacing * static_cast<float>(i);
        Line(Vec2{offset, -extent}, Vec2{offset, extent}, color, 0.0f,
             GizmoSpace::World, GizmoCategory::Grid);
        Line(Vec2{-extent, offset}, Vec2{extent, offset}, color, 0.0f,
             GizmoSpace::World, GizmoCategory::Grid);
    }
}

void Gizmos::OriginAxes(float length) {
    // Red for x and green for y, which is the convention every 3D tool uses.
    Line(Vec2{0.0f, 0.0f}, Vec2{length, 0.0f}, Color::Red(), 0.0f,
         GizmoSpace::World, GizmoCategory::Axes);
    Line(Vec2{0.0f, 0.0f}, Vec2{0.0f, length}, Color::Green(), 0.0f,
         GizmoSpace::World, GizmoCategory::Axes);
}

void Gizmos::Render(Camera& camera) {
    for (const GizmoCommand& command : g_commands) {
        if (!Visible(command)) {
            continue;
        }

        switch (command.shape) {
            case Shape::Line:
                Renderer::DrawLine(ToScreen(camera, command, command.a),
                                   ToScreen(camera, command, command.b), command.color);
                break;

            case Shape::Box:
            case Shape::FilledBox: {
                const Vec2 p0 = ToScreen(camera, command, command.a);
                const Vec2 p1 = ToScreen(camera, command, command.b);

                // The camera flips y, so the world's bottom-left corner becomes
                // the screen's TOP-left. Sorting the two points back into a
                // proper min and max matters because a rectangle with a
                // negative height draws as nothing at all.
                const Vec2 lo{std::min(p0.x, p1.x), std::min(p0.y, p1.y)};
                const Vec2 hi{std::max(p0.x, p1.x), std::max(p0.y, p1.y)};

                if (command.shape == Shape::Box) {
                    Renderer::DrawRect(lo, hi, command.color);
                } else {
                    Renderer::DrawFilledRect(lo, hi, command.color);
                }
                break;
            }

            case Shape::Circle: {
                const Vec2 centre = ToScreen(camera, command, command.a);

                // The radius is a LENGTH, not a place, so it goes through the
                // camera with WorldToScreenVector. Using WorldToScreen here
                // instead would add the camera's position to it and the circles
                // would slide off their own centres as the camera pans.
                const float radius =
                    (command.space == GizmoSpace::World)
                        ? camera.WorldToScreenVector(Vec2{command.b.x, 0.0f}).x
                        : command.b.x;

                // SDL can draw lines but not circles, so a circle is drawn as a
                // ring of short straight lines. More segments looks rounder.
                const int segments = std::max(3, g_circleSegments);
                Vec2 previous{centre.x + radius, centre.y};
                for (int i = 1; i <= segments; ++i) {
                    const float angle =
                        kTwoPi * static_cast<float>(i) / static_cast<float>(segments);
                    const Vec2 next{centre.x + std::cos(angle) * radius,
                                    centre.y + std::sin(angle) * radius};
                    Renderer::DrawLine(previous, next, command.color);
                    previous = next;
                }
                break;
            }

            case Shape::Text:
                Renderer::DrawText(ToScreen(camera, command, command.a),
                                   command.text.c_str(), command.color);
                break;
        }
    }
}

void Gizmos::EndFrame(float deltaSeconds) {
    // Take the elapsed time off every shape's remaining lifetime, then drop
    // the ones that have run out. A shape created with lifetime 0 is drawn
    // exactly once and then removed here, which is what "just this frame"
    // means.
    for (GizmoCommand& command : g_commands) {
        command.remaining -= deltaSeconds;
    }

    // std::erase_if removes every element matching the condition in one pass.
    // The alternative - looping with an index and calling erase() inside the
    // loop - shifts everything after the erased element and is a reliable way
    // to skip entries or read past the end.
    std::erase_if(g_commands, [](const GizmoCommand& command) {
        return command.remaining <= 0.0f;
    });
}

void Gizmos::Clear() {
    g_commands.clear();
}

void Gizmos::SetEnabled(bool on) { g_enabled = on; }
bool Gizmos::IsEnabled()         { return g_enabled; }

void Gizmos::SetCategoryEnabled(GizmoCategory category, bool on) {
    const int slot = static_cast<int>(category);
    if (slot >= 0 && slot < static_cast<int>(GizmoCategory::Count)) {
        g_categoryEnabled[slot] = on;
    }
}

bool Gizmos::IsCategoryEnabled(GizmoCategory category) {
    const int slot = static_cast<int>(category);
    if (slot < 0 || slot >= static_cast<int>(GizmoCategory::Count)) {
        return true;
    }
    return g_categoryEnabled[slot];
}

bool Gizmos::Init(const BootConfig& config) {
    SetCircleSegments(config.gizmoCircleSegments);
    return true;
}

void Gizmos::Shutdown() {
    Clear();
}

void Gizmos::SetCircleSegments(int segments) {
    g_circleSegments = std::clamp(segments, 3, 128);
}

int Gizmos::CircleSegments() { return g_circleSegments; }

} // namespace eng
