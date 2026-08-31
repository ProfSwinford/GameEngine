#pragma once

// =============================================================================
//  WEEK 6 - debug draw. Ch. 10.2.
//
//  *** The single highest-leverage system in the whole course. ***
//
//  It arrives in Week 6, before it is strictly needed, because every week
//  after this one is visually debuggable:
//    Week 7  - allocator statistics on screen
//    Week 9  - entity bounds and resource load states
//    Week 10 - every collider, every frame, plus the profiler HUD
//    Phase 2 - the first thing you reach for when the game misbehaves
//
//  ---------------------------------------------------------------------------
//  THE THREE REQUIREMENTS THAT MAKE IT USEFUL:
//
//  1. CALLABLE FROM ANYWHERE, with no renderer pointer threaded through the
//     call. Everything below is static and takes no context. A debug draw you
//     can only call from the render loop is a debug draw you cannot call from
//     the place the bug is - and the place the bug is, is usually a physics
//     update or a constructor.
//
//  2. WORLD SPACE OR SCREEN SPACE, per call. World shapes move with the
//     camera; screen-space HUD elements do not.
//
//  3. A LIFETIME IN SECONDS, per call. 0 means this frame only - the usual
//     case, called every frame from an update. 3 seconds means "mark this spot
//     and let me go and look at it", which is how you debug an event that
//     happened once and is over. That is what makes this a QUEUE rather than a
//     set of immediate draw calls, and it is the design Ch. 10.2 describes.
//
//  ---------------------------------------------------------------------------
//  DEBUGDRAW AND IMGUI ARE NOT THE SAME TOOL and never overlap:
//
//    DebugDraw - WORLD space, lifetimed, called from anywhere in engine code
//    ImGui     - SCREEN space, this frame only, called from the editor
//
//  ImGui cannot put a three-second marker at a world position. DebugDraw
//  cannot do a dockable table with a text filter. There is no ImGui in this
//  header or its .cpp, and no DebugDraw inside a panel.
//
//  Text uses SDL3's built-in 8x8 debug font via Renderer::DrawDebugText - no
//  font file, no SDL_ttf, no asset pipeline.
// =============================================================================

#include <engine/math/Mat3.h>
#include <engine/math/Overlap.h>
#include <engine/math/Vec2.h>
#include <engine/platform/Renderer.h>

namespace eng {

class Camera;

enum class DebugSpace : u8 {
    World,   // moves with the camera
    Screen,  // pinned to the window, for HUDs
};

// Categories exist so the editor's Debug Draw panel can switch groups of
// shapes off without the caller knowing a panel exists. A call with no
// category is Default and is always on unless the master switch is off.
enum class DebugCategory : u8 {
    Default,
    Grid,
    Axes,
    Bounds,
    Colliders,   // Week 10
    Count,
};

const char* ToString(DebugCategory category);

class DebugDraw {
public:
    static void Line(Vec2 a, Vec2 b, Color color, f32 lifetimeSeconds = 0.0f,
                     DebugSpace space = DebugSpace::World,
                     DebugCategory category = DebugCategory::Default);

    static void Box(const AABB& box, Color color, f32 lifetimeSeconds = 0.0f,
                    DebugSpace space = DebugSpace::World,
                    DebugCategory category = DebugCategory::Default);

    static void FilledBox(const AABB& box, Color color, f32 lifetimeSeconds = 0.0f,
                          DebugSpace space = DebugSpace::World,
                          DebugCategory category = DebugCategory::Default);

    static void Circle(Vec2 centre, f32 radius, Color color, f32 lifetimeSeconds = 0.0f,
                       DebugSpace space = DebugSpace::World,
                       DebugCategory category = DebugCategory::Default);

    static void Text(Vec2 position, const char* text, Color color,
                     f32 lifetimeSeconds = 0.0f,
                     DebugSpace space = DebugSpace::Screen,
                     DebugCategory category = DebugCategory::Default);

    // Draws a shape THROUGH A WORLD MATRIX the caller already had. This is the
    // one that matters for Week 10: every collider is drawn by handing over
    // the transform it already computed, so there is no second code path that
    // recomputes positions specially for debug geometry. The Week 6 evidence
    // document asks about exactly this.
    static void TransformedBox(const Mat3& worldMatrix, Vec2 halfExtents, Color color,
                               f32 lifetimeSeconds = 0.0f,
                               DebugCategory category = DebugCategory::Colliders);

    // A world grid and origin axes. Twenty minutes of work that gets used
    // constantly from Week 6 onward - "where IS the origin" is the first
    // question every rendering bug asks.
    static void Grid(f32 spacing, Color color, i32 halfLines = 20);
    static void OriginAxes(f32 length = 100.0f);

    // Draws the queue through `camera`. Called AFTER everything else has
    // rendered, so debug geometry lands on top.
    //
    // *** THIS NO LONGER AGES OR CONSUMES THE QUEUE. *** It used to do both,
    // which was fine while there was exactly one view - and silently broke the
    // moment the editor grew a Scene view and a Game view, because the second
    // pass would draw an empty queue. Splitting drawing from expiry is what
    // lets the same commands appear in as many views as want them.
    static void Render(Camera& camera);

    // Advances lifetimes and drops expired commands. Called ONCE per frame,
    // after every view has drawn.
    static void EndFrame(f32 deltaSeconds);

    // Drops everything. Week 9's scene unload calls this, otherwise a
    // three-second marker outlives the entity it was marking.
    static void Clear();

    // --- editor-facing switches -------------------------------------------
    static void SetEnabled(bool on);
    static bool IsEnabled();
    static void SetCategoryEnabled(DebugCategory category, bool on);
    static bool IsCategoryEnabled(DebugCategory category);

    // Circle tessellation. 24 segments is the default and looks acceptable to
    // about 200 pixels of radius. Exposed here and wired to the
    // debug.circleSegments CVar in Week 8, so the Debug Draw panel's slider
    // can show tessellation changing live.
    static void SetCircleSegments(i32 segments);
    static i32  CircleSegments();

    // Instrumentation: how many commands are queued and how many survived from
    // previous frames. The second number is how you notice that something is
    // enqueuing a three-second marker every frame.
    // sizeof/alignof of the queued command struct, for the Week 4 audit and for
    // the Debug Draw panel's "what does this queue cost" line. The type itself
    // stays private to DebugDraw.cpp - only its footprint is public.
    static usize CommandSizeBytes();
    static usize CommandAlignBytes();

    static usize QueuedCommandCount();
    static usize PersistentCommandCount();
};

} // namespace eng
