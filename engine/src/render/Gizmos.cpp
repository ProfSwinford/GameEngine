// =============================================================================
//  Gizmos.cpp - A SHELL. The declarations are real; the bodies are yours to write.
//
//  Everything here compiles and links, so the editor builds and runs from day
//  one. It just does not do this part yet: each function returns a harmless
//  neutral value so nothing crashes and nothing lies about having worked.
//
//  Fill these in as the course reaches them. The header this file implements
//  explains WHAT each function is for and WHY it exists - read it first.
// =============================================================================

#include <engine/render/Gizmos.h>

namespace eng {
namespace {
bool g_enabled        = true;
int  g_circleSegments = 24;
} // namespace

const char* ToString(GizmoCategory /*category*/) { return "Gizmo"; }

// TODO: every one of these should QUEUE a shape rather than draw it, because a
// shape has to be able to outlive the call that created it - that is what the
// lifetime argument is for. See Gizmos.h.
void Gizmos::Line(Vec2, Vec2, Color, float, GizmoSpace, GizmoCategory) {}
void Gizmos::Box(const AABB&, Color, float, GizmoSpace, GizmoCategory) {}
void Gizmos::FilledBox(const AABB&, Color, float, GizmoSpace, GizmoCategory) {}
void Gizmos::Circle(Vec2, float, Color, float, GizmoSpace, GizmoCategory) {}
void Gizmos::Text(Vec2, const std::string&, Color, float, GizmoSpace, GizmoCategory) {}
void Gizmos::TransformedBox(const Mat3&, Vec2, Color, float, GizmoCategory) {}
void Gizmos::Grid(float, Color, int) {}
void Gizmos::OriginAxes(float) {}

// TODO: draw the queue, then age it. Render happens once per VIEW; EndFrame
// happens once per FRAME, after every view has drawn the same queue.
void Gizmos::Render(Camera& /*camera*/) {}
void Gizmos::EndFrame(float /*deltaSeconds*/) {}
void Gizmos::Clear() {}

void Gizmos::SetEnabled(bool on) { g_enabled = on; }
bool Gizmos::IsEnabled()         { return g_enabled; }

void Gizmos::SetCategoryEnabled(GizmoCategory, bool) {}
bool Gizmos::IsCategoryEnabled(GizmoCategory)        { return true; }

void Gizmos::SetCircleSegments(int segments) { g_circleSegments = segments; }
int  Gizmos::CircleSegments()                { return g_circleSegments; }

} // namespace eng
