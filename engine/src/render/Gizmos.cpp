// =============================================================================
//  Gizmos.cpp - a skeleton. Every function is here with the right signature and
//  an empty body. Gizmos.h is the specification; read it before filling one in.
//
//  Every shape is QUEUED rather than drawn straight away, because a shape has to
//  be able to outlive the call that created it - that is what the lifetime
//  argument is for, and it is why this is a queue and not a set of draw calls.
// =============================================================================

#include <engine/render/Gizmos.h>

namespace eng {

// Turns a category into a readable name, for the editor's Gizmos menu.
const char* ToString(GizmoCategory /*category*/) {
    return "Default";
}

// Queues a line between two points.
void Gizmos::Line(Vec2 /*a*/, Vec2 /*b*/, Color /*color*/, float /*lifetimeSeconds*/,
                  GizmoSpace /*space*/, GizmoCategory /*category*/) {
}

// Queues the outline of a box - what a collider is drawn as in the Scene view.
void Gizmos::Box(const AABB& /*box*/, Color /*color*/, float /*lifetimeSeconds*/,
                 GizmoSpace /*space*/, GizmoCategory /*category*/) {
}

// Queues a solid box, for highlighting rather than outlining.
void Gizmos::FilledBox(const AABB& /*box*/, Color /*color*/, float /*lifetimeSeconds*/,
                       GizmoSpace /*space*/, GizmoCategory /*category*/) {
}

// Queues a circle, drawn as a ring of straight lines.
void Gizmos::Circle(Vec2 /*centre*/, float /*radius*/, Color /*color*/,
                    float /*lifetimeSeconds*/, GizmoSpace /*space*/,
                    GizmoCategory /*category*/) {
}

// Queues a line of text. Defaults to screen space, because a label that scales
// with the zoom is rarely what anybody wants.
void Gizmos::Text(Vec2 /*position*/, const std::string& /*text*/, Color /*color*/,
                  float /*lifetimeSeconds*/, GizmoSpace /*space*/,
                  GizmoCategory /*category*/) {
}

// Queues a box pushed through a world matrix, so it leans when the entity is
// rotated instead of staying upright.
void Gizmos::TransformedBox(const Mat3& /*worldMatrix*/, Vec2 /*halfExtents*/,
                            Color /*color*/, float /*lifetimeSeconds*/,
                            GizmoCategory /*category*/) {
}

// Queues the background grid the Scene view draws.
void Gizmos::Grid(float /*spacing*/, Color /*color*/, int /*halfLines*/) {
}

// Queues the red and green arrows showing where the origin is and which way the
// axes point.
void Gizmos::OriginAxes(float /*length*/) {
}

// Draws everything currently queued, through the given camera. Called once per
// view, so the editor draws the same queue twice.
void Gizmos::Render(Camera& /*camera*/) {
}

// Ages the queue and drops anything whose lifetime has run out. Called once per
// FRAME, after every view has drawn - do it inside the drawing and whichever
// view goes second finds nothing left.
void Gizmos::EndFrame(float /*deltaSeconds*/) {
}

// Throws the whole queue away.
void Gizmos::Clear() {
}

// Turns all helper shapes on or off - the Scene view's Gizmos button.
void Gizmos::SetEnabled(bool /*on*/) {
}

// Are helper shapes being drawn at all?
bool Gizmos::IsEnabled() {
    return false;
}

// Turns one category on or off, so collider outlines can be hidden while the
// grid stays.
void Gizmos::SetCategoryEnabled(GizmoCategory /*category*/, bool /*on*/) {
}

// Is this category being drawn?
bool Gizmos::IsCategoryEnabled(GizmoCategory /*category*/) {
    return false;
}

// Sets how many straight lines a circle is drawn with - the trade between how
// round it looks and how much work it is.
void Gizmos::SetCircleSegments(int /*segments*/) {
}

// How many lines a circle is currently drawn with.
int Gizmos::CircleSegments() {
    return 0;
}

} // namespace eng
