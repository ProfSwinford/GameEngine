#pragma once

// ============================================================================
//  Rendering.h - textures, the camera, the drawing calls, and the debug gizmos.
//
//  Each section below was its own file until the engine was reorganised; the
//  banner at the top of each one still explains that piece on its own.
// ============================================================================

#include <engine/modules/Math.h>
#include <engine/modules/Platform.h>
#include <engine/modules/Subsystem.h>

#include <memory>
#include <string>

// ============================================================================
//  Texture.h - a loaded image, and the shared pointer used to refer to one.
//
//  A texture is a picture that has been read off disk and handed to the
//  graphics card, ready to be drawn. Sprites in the scene refer to textures;
//  the asset browser previews them; the same picture is usually used by many
//  entities at once.
//
//  WHY std::shared_ptr AND NOT A RAW Texture*
//  "Used by many entities at once" is the whole problem. A raw pointer gives
//  no answer to "when is it safe to unload this?" - if one entity unloads a
//  texture the others are still pointing at it, and drawing through a pointer
//  to something that has been freed is a crash (or worse, silently wrong
//  pixels).
//
//  std::shared_ptr is the standard library's answer. It counts how many
//  owners a thing currently has, and destroys it automatically when the last
//  one lets go. Copy a TextureRef and the count goes up; let one go out of
//  scope and it goes down. Nobody has to remember to unload anything, and
//  there is no way to end up holding a pointer to a texture that is gone.
//
//  You can see the count live in the Inspector, which is the clearest way to
//  understand what shared ownership means.
// ============================================================================


namespace eng {

struct Texture {
    // Where it was loaded from, e.g. "textures/player.bmp". Kept so the
    // texture can be saved back into a scene file and shown in the Inspector.
    std::string path;

    int width  = 0;
    int height = 0;

    // The graphics-card object SDL created for this image.
    //
    // Stored as void* so that this header does not have to mention SDL. The
    // drawing code casts it back; nothing else touches it.
    void* native = nullptr;

    // True when this is the stand-in magenta square shown for an image that
    // could not be loaded. The Inspector uses it to say so in red rather than
    // leaving you to wonder why your sprite is magenta.
    bool isPlaceholder = false;

    // Destroys the graphics-card object. Called automatically by shared_ptr
    // when the last owner lets go.
    ~Texture();
};

// The type everything else uses. Read it as "a shared reference to a texture".
using TextureRef = std::shared_ptr<Texture>;

} // namespace eng


// ============================================================================
//  Camera.h - decides which part of the world ends up on screen.
//
//  A camera has a position (what it is looking at) and a zoom (how close it
//  is). Moving it moves the view; increasing the zoom makes everything bigger.
//
//  HOW IT WORKS: IT IS A TRANSFORM, BACKWARDS
//  The tempting way to write a camera is to subtract its position inside the
//  drawing code. That works until you add zoom, then rotation, then a parent,
//  and each one needs another special case somewhere else.
//
//  Instead, a camera at position p with zoom z is described by the matrix that
//  UNDOES placing an object at p and scaling it by z. Once it is a matrix,
//  Mat3 does all of the work: panning and zooming combine correctly for free,
//  and turning a mouse click back into a world position is just the inverse
//  matrix.
//
//  ==========================================================================
//  THE ONE PLACE THE Y AXIS FLIPS.
//
//  The world is Y-UP: bigger y means further up, which is what makes the maths
//  behave the way it does in a maths lesson and a positive rotation turn
//  anticlockwise. The screen is Y-DOWN, because every 2D graphics API in
//  existence puts pixel (0,0) in the top-left corner.
//
//  ViewMatrix is the only place in the whole engine that reconciles those two.
//  If y ever gets flipped somewhere else as well, the two flips cancel out and
//  everything ends up upside down in a way that looks almost right.
//  ==========================================================================
// ============================================================================


namespace eng {

class Camera {
public:
    Vec2  Position()     const { return m_position; }
    float Zoom()         const { return m_zoom; }
    Vec2  ViewportSize() const { return m_viewport; }

    void SetPosition(Vec2 position) { m_position = position; }
    void Move(Vec2 delta)           { m_position += delta; }

    // Zoom is kept away from zero. A zoom of exactly 0 would squash the whole
    // world onto a single point, and the matrix describing that cannot be
    // undone - so dragging a zoom slider to the bottom would break the ability
    // to click on anything. Clamping means it just gets very small.
    void SetZoom(float zoom);

    // How large the picture being drawn is, in pixels. The editor sets this
    // from the size of the panel; the standalone game sets it from the window.
    void SetViewportSize(Vec2 sizePixels) { m_viewport = sizePixels; }

    void Reset();

    // World coordinates to screen pixels, and back again.
    Mat3 ViewMatrix() const;
    Mat3 InverseViewMatrix() const;

    Vec2 WorldToScreen(Vec2 world) const;

    // The one that makes clicking work: given a pixel the mouse is over, which
    // point in the world is under it?
    Vec2 ScreenToWorld(Vec2 screen) const;

    // For a DIRECTION rather than a position - the camera's position does not
    // apply, only its zoom and the y flip. Used to turn a size in world units
    // into a size in pixels.
    Vec2 WorldToScreenVector(Vec2 world) const;

    // The rectangle of world the camera can currently see.
    AABB VisibleBounds() const;

private:
    Vec2  m_position{0.0f, 0.0f};
    float m_zoom = 1.0f;
    Vec2  m_viewport{1280.0f, 720.0f};
};

} // namespace eng


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


// ============================================================================
//  Gizmos.h - the helper shapes drawn on top of the world.
//
//  These are the same thing Unity calls Gizmos: the grid, the coloured origin
//  arrows, the outline around whatever is selected, the box showing where a
//  collider actually is. They appear in the Scene view so you can see what the
//  game is doing, and they are switched off in the Game view so you can see
//  what a player would see.
//
//  Anything can draw one, from anywhere:
//
//      Gizmos::Circle(enemy.Position(), 50.0f, Color::Red());
//
//  THREE THINGS MAKE THEM USEFUL
//
//  1. NO SETUP. Every function is static and needs no renderer, no camera and
//     no context passed in. A helper you can only call from the drawing code
//     is a helper you cannot call from the place the problem actually is,
//     which is usually somewhere in the middle of a physics update.
//
//  2. WORLD OR SCREEN, per call. A world shape moves with the camera - it is
//     attached to a place in the game. A screen shape stays put, which is what
//     an on-screen score or timer wants.
//
//  3. A LIFETIME IN SECONDS, per call. 0 means "just this frame", which is the
//     usual case when you call it every frame from an update. 3 means "mark
//     this spot and leave it there for three seconds so I can go and look at
//     it" - which is how you see something that happened once and was over
//     before you could look.
//
//  That third point is why this is a QUEUE of shapes rather than a set of
//  immediate draw calls: a shape has to be able to outlive the call that
//  created it.
//
//  GIZMOS AND THE EDITOR'S PANELS ARE DIFFERENT TOOLS
//    Gizmos - shapes in the game world, can last several seconds, callable
//             from any engine or game code
//    Panels - windows, buttons and tables, this frame only, part of the editor
//  Neither can do the other's job, and there is no overlap between them.
// ============================================================================


namespace eng {

class Camera;

// Whether a shape is placed in the game world or pinned to the screen.
enum class GizmoSpace {
    World,    // moves with the camera
    Screen,   // stays where it is, for scores and timers
};

// Groups, so the Scene view's Gizmos menu can switch a whole set of shapes off
// without the code that drew them knowing the menu exists.
enum class GizmoCategory {
    Default,
    Grid,
    Axes,
    Bounds,
    Colliders,
    Count,      // not a real category; it is how many there are
};

const char* ToString(GizmoCategory category);

class Gizmos : public Subsystem {
public:
    // Reads how round a drawn circle should look out of the settings.
    bool Init(const BootConfig& config) override;

    // Throws away anything still queued to be drawn.
    void Shutdown() override;

    static void Line(Vec2 a, Vec2 b, Color color, float lifetimeSeconds = 0.0f,
                     GizmoSpace space = GizmoSpace::World,
                     GizmoCategory category = GizmoCategory::Default);

    static void Box(const AABB& box, Color color, float lifetimeSeconds = 0.0f,
                    GizmoSpace space = GizmoSpace::World,
                    GizmoCategory category = GizmoCategory::Default);

    static void FilledBox(const AABB& box, Color color, float lifetimeSeconds = 0.0f,
                          GizmoSpace space = GizmoSpace::World,
                          GizmoCategory category = GizmoCategory::Default);

    static void Circle(Vec2 centre, float radius, Color color,
                       float lifetimeSeconds = 0.0f,
                       GizmoSpace space = GizmoSpace::World,
                       GizmoCategory category = GizmoCategory::Default);

    static void Text(Vec2 position, const std::string& text, Color color,
                     float lifetimeSeconds = 0.0f,
                     GizmoSpace space = GizmoSpace::Screen,
                     GizmoCategory category = GizmoCategory::Default);

    // Draws a box through a transform the caller already has, so a rotated
    // collider outline lines up with the rotated object.
    //
    // Taking the matrix rather than a position and an angle means there is no
    // second piece of code working out where things are - the outline is drawn
    // through exactly the same transform the object itself uses, so the two
    // cannot disagree.
    static void TransformedBox(const Mat3& worldMatrix, Vec2 halfExtents, Color color,
                               float lifetimeSeconds = 0.0f,
                               GizmoCategory category = GizmoCategory::Colliders);

    // The background grid and the red/green arrows at the world origin. Twenty
    // minutes of work that gets used constantly, because "where IS (0,0)?" is
    // the first question every drawing problem asks.
    static void Grid(float spacing, Color color, int halfLines = 20);
    static void OriginAxes(float length = 100.0f);

    // Draws everything currently queued, seen through `camera`. Called after
    // the world has been drawn, so gizmos land on top of it.
    //
    // This does NOT remove anything from the queue. That matters because the
    // editor draws the same queue twice - once for the Scene view and once for
    // the Game view - and whichever went second would otherwise find it empty.
    static void Render(Camera& camera);

    // Ages every shape and throws away the ones whose time is up. Called once
    // per frame, after every view has drawn.
    static void EndFrame(float deltaSeconds);

    // Throws everything away immediately. Loading a new scene does this, or a
    // three-second marker would outlive the object it was marking.
    static void Clear();

    // ---- switches the Scene view's Gizmos menu drives ---------------------
    static void SetEnabled(bool on);
    static bool IsEnabled();
    static void SetCategoryEnabled(GizmoCategory category, bool on);
    static bool IsCategoryEnabled(GizmoCategory category);

    // How many straight lines are used to draw a circle. More looks rounder
    // and costs more; 24 is smooth enough up to a couple of hundred pixels.
    static void SetCircleSegments(int segments);
    static int  CircleSegments();
};

} // namespace eng
