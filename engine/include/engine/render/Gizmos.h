#pragma once

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

#include <engine/core/Subsystem.h>
#include <engine/math/Mat3.h>
#include <engine/math/Overlap.h>
#include <engine/math/Vec2.h>
#include <engine/render/Renderer.h>

#include <cstddef>

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
    // ========================================================================
    //  A grid covering a world-space AREA.
    //
    //  This used to take a line count and draw a fixed box around the world
    //  ORIGIN, which meant the grid simply stopped once you panned past its
    //  edge - the camera would sail off into empty space with nothing to
    //  judge position against.
    //
    //  Taking an area instead puts the caller in charge of "what can be
    //  seen", which is the only thing that knows: the Scene view works it out
    //  from its camera and viewport, so the grid now covers the view however
    //  far it moves or zooms. See ScenePanel::RenderView.
    //
    //  The spacing is COARSENED automatically when the area would need too
    //  many lines to draw sensibly - the reason is in the implementation and
    //  it is worth reading.
    // ========================================================================
    static void Grid(const AABB& worldArea, float spacing, Color color);

    // The most lines Grid will draw across both axes before it starts
    // doubling the spacing instead.
    static constexpr int kGridMaxLines = 400;

    // The spacing Grid will ACTUALLY use for this area. Split out so the one
    // piece of arithmetic worth getting wrong can be tested on its own.
    static float GridStepFor(const AABB& worldArea, float spacing);
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

    // ======================================================================
    //  HOW MANY SHAPES ARE QUEUED RIGHT NOW.
    //
    //  THIS EXISTS BECAUSE "NOTHING WAS DRAWN" WAS A REAL BUG AND COULD NOT BE
    //  TESTED. The collider outlines used to be queued from the tail of
    //  CollisionSystem::Update, which runs only when the clock asks for a
    //  fixed step - so in the editor, where the clock is paused for the whole
    //  time you are building a scene, no collider was ever drawn. Nothing
    //  could ask whether a drawing call had produced anything, so nothing
    //  noticed for as long as it took somebody to report it.
    //
    //  It counts COMMANDS and not pixels: one Line is one, and one Circle is
    //  also one however many segments it is drawn with. That is the number
    //  worth having - "did the thing that was supposed to draw, draw" - and it
    //  is the same reason CollisionSystem::NarrowPhaseTests is public.
    //
    //  Nothing in the engine reads it. It is for tests and for an editor that
    //  wants to show the cost of its own overlay.
    // ======================================================================
    static std::size_t QueuedCount();

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
