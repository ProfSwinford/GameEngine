// WEEK 6 - debug draw. See DebugDraw.h.
//
// A queue of commands with lifetimes, flushed once per frame. No ImGui in this
// file, on purpose - see the header.

#include <engine/debug/Camera.h>
#include <engine/debug/DebugDraw.h>
#include <engine/debug/ScopedTimer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace eng {
namespace {

enum class Shape : u8 { Line, Box, FilledBox, Circle, Text };

// WEEK 4 SIZEOF AUDIT SUBJECT, and the largest one in the engine.
//
// This struct exists in the thousands: Week 10 enqueues one per collider per
// frame, plus 82 for the grid. `sandbox --sizeof-audit` reports its measured
// size, which is why DebugDraw exposes CommandSizeBytes() rather than the
// audit quoting a number somebody typed.
//
// The THREE 1-BYTE ENUMS ARE DECLARED LAST, TOGETHER, which is the reordering
// rule the audit arrived at. Interleaved with the 4- and 8-byte members they
// would each force their own run of padding; grouped at the end they share
// one. This is the only struct in the engine where the reordering actually
// saved anything - see docs/week04-sizeof-audit.md, which is candid that the
// other five it audits had no saving available and shows the arithmetic.
//
// The text array dominates the size and is a deliberate trade: a fixed
// char[48] rather than a std::string means a Text() call with a lifetime does
// not allocate and does not dangle when the caller's string dies - which is
// the entire point of a command that outlives its call site. A std::string
// would be 32 bytes here and a heap allocation per lifetimed label.
struct DebugCommand {
    Vec2          a{};                // start / min / centre / position
    Vec2          b{};                // end / max / (radius in b.x)
    f32           remaining = 0.0f;   // seconds; <= 0 means "this frame only"
    Color         color{};
    char          text[48]{};
    Shape         shape    = Shape::Line;
    DebugSpace    space    = DebugSpace::World;
    DebugCategory category = DebugCategory::Default;
};

std::vector<DebugCommand> g_commands;
bool  g_enabled = true;
bool  g_categoryEnabled[static_cast<usize>(DebugCategory::Count)] = {true, true, true, true, true};
i32   g_circleSegments = 24;
usize g_persistentCount = 0;

DebugCommand& Enqueue() {
    // Reserved once; clear() below keeps the capacity, so after warm-up this
    // never allocates. Week 8 requires the update path to be allocation-free
    // and debug draw is squarely in the update path.
    if (g_commands.capacity() == 0) {
        g_commands.reserve(1024);
    }
    g_commands.emplace_back();
    return g_commands.back();
}

bool Visible(const DebugCommand& command) {
    if (!g_enabled) {
        return false;
    }
    const auto slot = static_cast<usize>(command.category);
    return slot >= static_cast<usize>(DebugCategory::Count) || g_categoryEnabled[slot];
}

Vec2 ToScreen(const Camera& camera, const DebugCommand& command, Vec2 point) {
    return (command.space == DebugSpace::World) ? camera.WorldToScreen(point) : point;
}

} // namespace

const char* ToString(DebugCategory category) {
    switch (category) {
        case DebugCategory::Default:   return "Default";
        case DebugCategory::Grid:      return "Grid";
        case DebugCategory::Axes:      return "Axes";
        case DebugCategory::Bounds:    return "Bounds";
        case DebugCategory::Colliders: return "Colliders";
        case DebugCategory::Count:     break;
    }
    return "?";
}

void DebugDraw::Line(Vec2 a, Vec2 b, Color color, f32 lifetimeSeconds, DebugSpace space,
                     DebugCategory category) {
    DebugCommand& command = Enqueue();
    command.shape     = Shape::Line;
    command.a         = a;
    command.b         = b;
    command.color     = color;
    command.remaining = lifetimeSeconds;
    command.space     = space;
    command.category  = category;
}

void DebugDraw::Box(const AABB& box, Color color, f32 lifetimeSeconds, DebugSpace space,
                    DebugCategory category) {
    DebugCommand& command = Enqueue();
    command.shape     = Shape::Box;
    command.a         = box.min;
    command.b         = box.max;
    command.color     = color;
    command.remaining = lifetimeSeconds;
    command.space     = space;
    command.category  = category;
}

void DebugDraw::FilledBox(const AABB& box, Color color, f32 lifetimeSeconds,
                          DebugSpace space, DebugCategory category) {
    DebugCommand& command = Enqueue();
    command.shape     = Shape::FilledBox;
    command.a         = box.min;
    command.b         = box.max;
    command.color     = color;
    command.remaining = lifetimeSeconds;
    command.space     = space;
    command.category  = category;
}

void DebugDraw::Circle(Vec2 centre, f32 radius, Color color, f32 lifetimeSeconds,
                       DebugSpace space, DebugCategory category) {
    DebugCommand& command = Enqueue();
    command.shape     = Shape::Circle;
    command.a         = centre;
    command.b         = Vec2{radius, 0.0f};
    command.color     = color;
    command.remaining = lifetimeSeconds;
    command.space     = space;
    command.category  = category;
}

void DebugDraw::Text(Vec2 position, const char* text, Color color, f32 lifetimeSeconds,
                     DebugSpace space, DebugCategory category) {
    DebugCommand& command = Enqueue();
    command.shape     = Shape::Text;
    command.a         = position;
    command.color     = color;
    command.remaining = lifetimeSeconds;
    command.space     = space;
    command.category  = category;

    if (text != nullptr) {
        // Copy, do not point. A command with a lifetime outlives the caller's
        // string, and a dangling char* here would be a use-after-free that
        // only shows up for shapes that persist - i.e. rarely, and confusingly.
        std::snprintf(command.text, sizeof(command.text), "%s", text);
    }
}

void DebugDraw::TransformedBox(const Mat3& worldMatrix, Vec2 halfExtents, Color color,
                               f32 lifetimeSeconds, DebugCategory category) {
    // Four corners through the matrix the caller already had. No separate
    // position computation, which is the property the Week 6 evidence document
    // asks about and the reason Week 10's collider drawing was free.
    const Vec2 corners[4] = {
        worldMatrix.TransformPoint(Vec2{-halfExtents.x, -halfExtents.y}),
        worldMatrix.TransformPoint(Vec2{ halfExtents.x, -halfExtents.y}),
        worldMatrix.TransformPoint(Vec2{ halfExtents.x,  halfExtents.y}),
        worldMatrix.TransformPoint(Vec2{-halfExtents.x,  halfExtents.y}),
    };
    for (int i = 0; i < 4; ++i) {
        Line(corners[i], corners[(i + 1) % 4], color, lifetimeSeconds, DebugSpace::World,
             category);
    }
}

void DebugDraw::Grid(f32 spacing, Color color, i32 halfLines) {
    if (spacing <= 0.0f) {
        return;
    }
    const f32 extent = spacing * static_cast<f32>(halfLines);
    for (i32 i = -halfLines; i <= halfLines; ++i) {
        const f32 offset = spacing * static_cast<f32>(i);
        Line(Vec2{offset, -extent}, Vec2{offset, extent}, color, 0.0f, DebugSpace::World,
             DebugCategory::Grid);
        Line(Vec2{-extent, offset}, Vec2{extent, offset}, color, 0.0f, DebugSpace::World,
             DebugCategory::Grid);
    }
}

void DebugDraw::OriginAxes(f32 length) {
    Line(Vec2{0.0f, 0.0f}, Vec2{length, 0.0f}, Color::Red(), 0.0f, DebugSpace::World,
         DebugCategory::Axes);
    Line(Vec2{0.0f, 0.0f}, Vec2{0.0f, length}, Color::Green(), 0.0f, DebugSpace::World,
         DebugCategory::Axes);
}

void DebugDraw::Render(Camera& camera) {
    ENGINE_SCOPED_TIMER("DebugDraw::Render");

    for (const DebugCommand& command : g_commands) {
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
                // Both corners through the camera, then re-normalised: the y
                // flip swaps which one is the visual minimum, and a rectangle
                // with a negative height draws as nothing.
                const Vec2 p0 = ToScreen(camera, command, command.a);
                const Vec2 p1 = ToScreen(camera, command, command.b);
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
                // Radius through the camera as a VECTOR, so the translation
                // does not apply. Getting this wrong makes circles drift off
                // their centres as the camera pans - which is TransformVector
                // versus TransformPoint, the Week 6 distinction, arriving in
                // practice.
                const f32 radius =
                    (command.space == DebugSpace::World)
                        ? camera.WorldToScreenVector(Vec2{command.b.x, 0.0f}).x
                        : command.b.x;

                const i32 segments = std::max(3, g_circleSegments);
                Vec2 previous{centre.x + radius, centre.y};
                for (i32 i = 1; i <= segments; ++i) {
                    const f32 angle =
                        kTwoPi * static_cast<f32>(i) / static_cast<f32>(segments);
                    const Vec2 next{centre.x + std::cos(angle) * radius,
                                    centre.y + std::sin(angle) * radius};
                    Renderer::DrawLine(previous, next, command.color);
                    previous = next;
                }
                break;
            }

            case Shape::Text:
                Renderer::DrawDebugText(ToScreen(camera, command, command.a), command.text,
                                        command.color);
                break;
        }
    }

}

void DebugDraw::EndFrame(f32 deltaSeconds) {
    // Age everything, then drop what expired. A command with lifetime 0 is
    // dropped after being drawn exactly once, which is the "called every frame
    // from an update" case; anything with a positive lifetime survives until
    // it runs out.
    //
    // SEPARATE FROM Render because the editor draws the same queue into both
    // the Scene view and the Game view. Expiring inside Render meant whichever
    // view drew second got nothing.
    //
    // std::erase_if in one pass rather than an index loop with erase() inside
    // it - which is the iterator-invalidation bug Week 10 is about, and this
    // is the version that does not have it.
    for (DebugCommand& command : g_commands) {
        command.remaining -= deltaSeconds;
    }
    std::erase_if(g_commands, [](const DebugCommand& command) {
        return command.remaining <= 0.0f;
    });

    g_persistentCount = g_commands.size();
}

void DebugDraw::Clear() {
    g_commands.clear();
    g_persistentCount = 0;
}

void DebugDraw::SetEnabled(bool on)  { g_enabled = on; }
bool DebugDraw::IsEnabled()          { return g_enabled; }

void DebugDraw::SetCategoryEnabled(DebugCategory category, bool on) {
    const auto slot = static_cast<usize>(category);
    if (slot < static_cast<usize>(DebugCategory::Count)) {
        g_categoryEnabled[slot] = on;
    }
}

bool DebugDraw::IsCategoryEnabled(DebugCategory category) {
    const auto slot = static_cast<usize>(category);
    return slot < static_cast<usize>(DebugCategory::Count) ? g_categoryEnabled[slot] : true;
}

void DebugDraw::SetCircleSegments(i32 segments) {
    g_circleSegments = std::clamp(segments, 3, 128);
}

i32 DebugDraw::CircleSegments() { return g_circleSegments; }

usize DebugDraw::CommandSizeBytes()  { return sizeof(DebugCommand); }
usize DebugDraw::CommandAlignBytes() { return alignof(DebugCommand); }

usize DebugDraw::QueuedCommandCount()     { return g_commands.size(); }
usize DebugDraw::PersistentCommandCount() { return g_persistentCount; }

} // namespace eng
