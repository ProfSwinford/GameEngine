// ============================================================================
//  ColliderHandles.cpp - the handles themselves. See ColliderHandles.h for why
//  they are a mode rather than always on.
// ============================================================================

#include <ColliderHandles.h>
#include <EditorApp.h>

#include <engine/physics/Collider.h>
#include <engine/render/Camera.h>
#include <engine/scene/Entity.h>
#include <engine/scene/Scene.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace editor {
namespace {

// THE SAME SIZE AS THE MOVE GIZMO'S, in screen pixels, and for the same
// reason it gives there: a handle that shrinks as you zoom out becomes
// unusable exactly when you need it most. Slightly smaller, because a polygon
// can have eight of these and they should not merge into one blob.
constexpr float kHandleHalf  = 5.0f;
constexpr float kGrabPadding = 3.0f;

ImVec2 Add(const ImVec2& a, const eng::Vec2& b) {
    return ImVec2(a.x + b.x, a.y + b.y);
}

// ---------------------------------------------------------------------------
//  WHAT A HANDLE IS.
//
//  Each one knows where it sits in the OWNER'S LOCAL SPACE - the space the
//  collider's own WorldShapes feeds to the world matrix - and what dragging
//  it means. Working in local space rather than in world space is what lets a
//  rotated, scaled, parented entity work with no special cases: the mouse
//  goes back through the same matrix and lands in the space the numbers are
//  stored in.
// ---------------------------------------------------------------------------
enum class HandleKind {
    Point,        // one entry in a polygon's or an edge's list
    SideLeft,     // a box's four sides, each moving independently
    SideRight,
    SideBottom,
    SideTop,
    Radius,       // a circle
    Breadth,      // a capsule's short axis
    Length,       // a capsule's long axis
};

struct Handle {
    HandleKind kind  = HandleKind::Point;
    int        index = 0;
    eng::Vec2  local{};
};

// Which handle is currently held. A free-standing struct rather than a member
// of ColliderEdit because it is about THIS DRAG, not about which collider is
// in edit mode - the mode outlives any number of drags.
struct DragState {
    bool       active = false;
    HandleKind kind   = HandleKind::Point;
    int        index  = 0;
};

DragState g_drag;
int       g_hoveredHandle = -1;

// ---------------------------------------------------------------------------
//  WHERE THE HANDLES ARE, worked out in ONE place and used by both the input
//  pass and the drawing pass.
//
//  The move gizmo's GizmoScreenFor makes the same argument in the same file:
//  two pieces of code computing this separately eventually disagree, and the
//  result is a handle you can see and cannot grab.
// ---------------------------------------------------------------------------
std::vector<Handle> HandlesFor(const eng::ColliderComponent& collider) {
    std::vector<Handle> handles;
    const eng::Vec2     offset = collider.Offset();

    switch (collider.Shape()) {
        case eng::ColliderShape::Box: {
            const eng::Vec2 half = collider.ScaledSize() * 0.5f;
            handles.push_back({HandleKind::SideLeft, 0,
                               offset + eng::Vec2{-half.x, 0.0f}});
            handles.push_back({HandleKind::SideRight, 0,
                               offset + eng::Vec2{half.x, 0.0f}});
            handles.push_back({HandleKind::SideBottom, 0,
                               offset + eng::Vec2{0.0f, -half.y}});
            handles.push_back({HandleKind::SideTop, 0,
                               offset + eng::Vec2{0.0f, half.y}});
            break;
        }

        case eng::ColliderShape::Circle: {
            const auto& circle =
                static_cast<const eng::CircleColliderComponent&>(collider);
            // ONE HANDLE, because a circle has one number. Two would imply an
            // ellipse, which this engine cannot test for - see the note in
            // physics/Collider.h.
            handles.push_back({HandleKind::Radius, 0,
                               offset + eng::Vec2{circle.Radius(), 0.0f}});
            break;
        }

        case eng::ColliderShape::Capsule: {
            const eng::Vec2 size = collider.ScaledSize();
            handles.push_back({HandleKind::Breadth, 0,
                               offset + eng::Vec2{size.x * 0.5f, 0.0f}});
            handles.push_back({HandleKind::Length, 0,
                               offset + eng::Vec2{0.0f, size.y * 0.5f}});
            break;
        }

        case eng::ColliderShape::Polygon: {
            const auto& poly =
                static_cast<const eng::PolygonColliderComponent&>(collider);
            const eng::Vec2 size = collider.ScaledSize();
            const auto&     pts  = poly.Points();
            for (std::size_t i = 0; i < pts.size(); ++i) {
                handles.push_back({HandleKind::Point, static_cast<int>(i),
                                   offset + eng::Vec2{pts[i].x * size.x,
                                                      pts[i].y * size.y}});
            }
            break;
        }

        case eng::ColliderShape::Edge: {
            const auto& edge =
                static_cast<const eng::EdgeColliderComponent&>(collider);
            const eng::Vec2 size = collider.ScaledSize();
            const auto&     pts  = edge.Points();
            for (std::size_t i = 0; i < pts.size(); ++i) {
                handles.push_back({HandleKind::Point, static_cast<int>(i),
                                   offset + eng::Vec2{pts[i].x * size.x,
                                                      pts[i].y * size.y}});
            }
            break;
        }

        case eng::ColliderShape::Tilemap:
            // No shape of its own: it IS the TilemapComponent beside it, and
            // that is painted rather than dragged. Handles here would be
            // controls that move nothing.
            break;
    }
    return handles;
}

ImVec2 ScreenFor(const eng::Camera& camera, const ImVec2& imageOrigin,
                 const eng::Mat3& world, const eng::Vec2& local) {
    return Add(imageOrigin, camera.WorldToScreen(world.TransformPoint(local)));
}

bool Hit(const ImVec2& mouse, const ImVec2& at) {
    const float reach = kHandleHalf + kGrabPadding;
    return std::fabs(mouse.x - at.x) <= reach && std::fabs(mouse.y - at.y) <= reach;
}

// ---------------------------------------------------------------------------
//  APPLYING A DRAG.
//
//  `local` is where the mouse is in the owner's own space. Everything below
//  writes through the collider's public setters rather than touching fields,
//  so the clamping and the point-count limits those setters enforce cannot be
//  stepped around by dragging.
// ---------------------------------------------------------------------------
void ApplyDrag(eng::ColliderComponent& collider, const Handle& handle,
               const eng::Vec2& local) {
    const eng::Vec2 reference = collider.ReferenceSize();
    if (reference.x <= 0.0f || reference.y <= 0.0f) {
        return;
    }
    const eng::Vec2 offset = collider.Offset();
    const eng::Vec2 half   = collider.ScaledSize() * 0.5f;

    switch (handle.kind) {
        // ===================================================================
        //  A BOX'S SIDES MOVE ONE AT A TIME, WITH THE OPPOSITE ONE PINNED.
        //
        //  Which means a drag changes the scale AND the offset together -
        //  that is what "move this edge" means, and it is Unity's behaviour.
        //  Resizing symmetrically about the centre instead would make fitting
        //  a collider to a character's feet a matter of alternating between
        //  two fields until they converged.
        // ===================================================================
        case HandleKind::SideLeft:
        case HandleKind::SideRight: {
            const float pinned = (handle.kind == HandleKind::SideLeft)
                                     ? offset.x + half.x
                                     : offset.x - half.x;
            const float moved  = local.x;
            const float width  = std::fabs(moved - pinned);
            collider.SetScale(eng::Vec2{width / reference.x, collider.Scale().y});
            collider.SetOffset(eng::Vec2{(moved + pinned) * 0.5f, offset.y});
            break;
        }

        case HandleKind::SideBottom:
        case HandleKind::SideTop: {
            const float pinned = (handle.kind == HandleKind::SideBottom)
                                     ? offset.y + half.y
                                     : offset.y - half.y;
            const float moved  = local.y;
            const float height = std::fabs(moved - pinned);
            collider.SetScale(eng::Vec2{collider.Scale().x, height / reference.y});
            collider.SetOffset(eng::Vec2{offset.x, (moved + pinned) * 0.5f});
            break;
        }

        case HandleKind::Radius: {
            auto&       circle = static_cast<eng::CircleColliderComponent&>(collider);
            const float dx     = local.x - offset.x;
            const float dy     = local.y - offset.y;
            circle.SetRadius(std::sqrt(dx * dx + dy * dy));
            break;
        }

        // A capsule resizes about its centre, unlike a box. Its round ends
        // are defined from the short side, so an off-centre drag would change
        // the radius and the core length at once and the shape would wander
        // away from the cursor.
        case HandleKind::Breadth: {
            const float width = std::fabs(local.x - offset.x) * 2.0f;
            collider.SetScale(eng::Vec2{width / reference.x, collider.Scale().y});
            break;
        }

        case HandleKind::Length: {
            const float height = std::fabs(local.y - offset.y) * 2.0f;
            collider.SetScale(eng::Vec2{collider.Scale().x, height / reference.y});
            break;
        }

        // ===================================================================
        //  A POINT IS STORED AS A FRACTION, so the mouse is divided by the
        //  scaled box on the way in - the exact inverse of what WorldShapes
        //  does on the way out.
        //
        //  A COLLAPSED AXIS IS LEFT ALONE rather than divided by. A scale of
        //  zero on one axis is a legal thing to have typed, and dividing by
        //  it would put an infinity into the point list, from which the shape
        //  never comes back.
        // ===================================================================
        case HandleKind::Point: {
            const eng::Vec2 size = collider.ScaledSize();
            if (size.x == 0.0f || size.y == 0.0f) {
                return;
            }
            const eng::Vec2 fraction{(local.x - offset.x) / size.x,
                                     (local.y - offset.y) / size.y};

            if (collider.Shape() == eng::ColliderShape::Polygon) {
                auto& poly = static_cast<eng::PolygonColliderComponent&>(collider);
                std::vector<eng::Vec2> points = poly.Points();
                if (handle.index < 0 ||
                    static_cast<std::size_t>(handle.index) >= points.size()) {
                    return;
                }
                points[static_cast<std::size_t>(handle.index)] = fraction;
                poly.SetPoints(std::move(points));
            } else if (collider.Shape() == eng::ColliderShape::Edge) {
                auto& edge = static_cast<eng::EdgeColliderComponent&>(collider);
                std::vector<eng::Vec2> points = edge.Points();
                if (handle.index < 0 ||
                    static_cast<std::size_t>(handle.index) >= points.size()) {
                    return;
                }
                points[static_cast<std::size_t>(handle.index)] = fraction;
                edge.SetPoints(std::move(points));
            }
            break;
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
//  ColliderEdit
// ---------------------------------------------------------------------------

ColliderEdit& ColliderEdit::Get() {
    static ColliderEdit state;
    return state;
}

eng::ColliderComponent* ColliderEdit::Target() const {
    if (!m_active) {
        return nullptr;
    }
    eng::Scene* scene = eng::Scene::Active();
    if (scene == nullptr) {
        return nullptr;
    }
    eng::Entity* entity = scene->Get(m_entity);
    if (entity == nullptr || m_index >= entity->ComponentCount()) {
        return nullptr;
    }

    // A dynamic_cast rather than a stored pointer: the component at this
    // index may be a different one entirely after an undo, and the only
    // honest answer then is "not a collider, so nothing to edit".
    return dynamic_cast<eng::ColliderComponent*>(entity->ComponentAt(m_index));
}

bool ColliderEdit::IsEditing(const eng::ColliderComponent& collider) const {
    return Target() == &collider;
}

void ColliderEdit::Toggle(const eng::ColliderComponent& collider) {
    if (IsEditing(collider)) {
        Stop();
        return;
    }

    const eng::Entity* owner = collider.Owner();
    if (owner == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < owner->ComponentCount(); ++i) {
        if (owner->ComponentAt(i) == &collider) {
            m_entity = owner->Id();
            m_index  = i;
            m_active = true;
            g_drag   = DragState{};
            return;
        }
    }
}

void ColliderEdit::Stop() {
    m_active = false;
    g_drag   = DragState{};
}

// ---------------------------------------------------------------------------
//  Input
// ---------------------------------------------------------------------------

bool UpdateColliderHandles(const eng::Camera& camera, const ImVec2& imageOrigin) {
    g_hoveredHandle = -1;

    eng::ColliderComponent* collider = ColliderEdit::Get().Target();
    if (collider == nullptr) {
        g_drag = DragState{};
        return false;
    }
    eng::Entity* owner = collider->Owner();
    if (owner == nullptr) {
        return false;
    }

    // RELEASED FIRST, before anything else can decide the mouse is still
    // held. The move gizmo does the same and for the same reason: a drag that
    // outlives the button is a shape that follows the cursor around the
    // screen.
    if (g_drag.active && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        g_drag = DragState{};
    }

    const eng::Mat3           world   = owner->Transform().WorldMatrix();
    const std::vector<Handle> handles = HandlesFor(*collider);
    const ImGuiIO&            io      = ImGui::GetIO();
    const ImVec2 mouse{io.MousePos.x, io.MousePos.y};

    if (g_drag.active) {
        // The held handle is found again by kind and index rather than kept
        // as a pointer, because the list is rebuilt every frame from a shape
        // this very drag is changing.
        for (const Handle& handle : handles) {
            if (handle.kind != g_drag.kind || handle.index != g_drag.index) {
                continue;
            }
            const eng::Vec2 screen{mouse.x - imageOrigin.x, mouse.y - imageOrigin.y};
            const eng::Vec2 inWorld = camera.ScreenToWorld(screen);
            const eng::Vec2 local   = world.Inverse().TransformPoint(inWorld);

            ApplyDrag(*collider, handle, local);

            // Every frame of the drag, and still ONE undo step: the history
            // only takes a snapshot on a frame where no mouse button is down.
            // See UndoStack.h.
            EditorState::Get().MarkDirty();
            return true;
        }
        g_drag = DragState{};
        return true;
    }

    for (std::size_t i = 0; i < handles.size(); ++i) {
        const ImVec2 at = ScreenFor(camera, imageOrigin, world, handles[i].local);
        if (!Hit(mouse, at)) {
            continue;
        }
        g_hoveredHandle = static_cast<int>(i);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            g_drag.active = true;
            g_drag.kind   = handles[i].kind;
            g_drag.index  = handles[i].index;
        }

        // TAKEN EVEN ON A HOVER, not only on a click. Picking runs after this
        // and tests whether the mouse went down this frame - so reporting
        // "not mine" while hovering would let the click through to the pick
        // underneath on the very frame the grab started.
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
//  Drawing
// ---------------------------------------------------------------------------

void DrawColliderHandles(const eng::Camera& camera, const ImVec2& imageOrigin) {
    eng::ColliderComponent* collider = ColliderEdit::Get().Target();
    if (collider == nullptr) {
        return;
    }
    eng::Entity* owner = collider->Owner();
    if (owner == nullptr) {
        return;
    }

    const eng::Mat3           world   = owner->Transform().WorldMatrix();
    const std::vector<Handle> handles = HandlesFor(*collider);
    ImDrawList*               draw    = ImGui::GetWindowDrawList();

    const ImU32 idle    = IM_COL32(255, 210, 90, 255);
    const ImU32 hot     = IM_COL32(255, 255, 255, 255);
    const ImU32 outline = IM_COL32(20, 20, 24, 200);

    for (std::size_t i = 0; i < handles.size(); ++i) {
        const ImVec2 at = ScreenFor(camera, imageOrigin, world, handles[i].local);

        const bool held = g_drag.active && handles[i].kind == g_drag.kind &&
                          handles[i].index == g_drag.index;
        const bool lit = held || g_hoveredHandle == static_cast<int>(i);

        const ImVec2 lo(at.x - kHandleHalf, at.y - kHandleHalf);
        const ImVec2 hi(at.x + kHandleHalf, at.y + kHandleHalf);

        // A DARK EDGE UNDER THE FILL. These sit on top of whatever the game
        // looks like, and a yellow square on yellow art is invisible.
        draw->AddRectFilled(ImVec2(lo.x - 1.0f, lo.y - 1.0f),
                            ImVec2(hi.x + 1.0f, hi.y + 1.0f), outline);
        draw->AddRectFilled(lo, hi, lit ? hot : idle);
    }
}

} // namespace editor
