// THE SCENE VIEW. See ScenePanel.h for the camera separation and the controls.

#include "panels/ScenePanel.h"

#include "AssetDragDrop.h"
#include "EditorApp.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace editor {
namespace {

// Screen-space size of the gizmo, in pixels. Fixed rather than scaled with
// zoom, which is the whole reason it is drawn in ImGui rather than in world
// space - a handle that shrinks as you zoom out becomes unusable exactly when
// you need it most.
constexpr float kAxisLength   = 60.0f;
constexpr float kHandleRadius = 7.0f;
constexpr float kCentreHalf   = 9.0f;

ImVec2 Add(const ImVec2& a, const eng::Vec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }

// Where the three handles are on screen. Computed identically by the input
// pass and the draw pass, which is the point of having it in one place: a
// gizmo you can see and cannot grab is what you get when those two disagree.
struct GizmoScreen {
    ImVec2 origin;
    ImVec2 xEnd;
    ImVec2 yEnd;
};

GizmoScreen GizmoScreenFor(const eng::Camera& camera, const ImVec2& imageOrigin,
                           const eng::Vec2& world) {
    GizmoScreen g;
    g.origin = Add(imageOrigin, camera.WorldToScreen(world));
    // +x is right; -y is UP, because the camera flipped the axis on the way in
    // and the gizmo has to agree with what is drawn under it.
    g.xEnd = ImVec2(g.origin.x + kAxisLength, g.origin.y);
    g.yEnd = ImVec2(g.origin.x, g.origin.y - kAxisLength);
    return g;
}

bool NearSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b, float tolerance) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float lengthSq = dx * dx + dy * dy;
    if (lengthSq <= 0.0001f) {
        return false;
    }
    float t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSq;
    t = std::clamp(t, 0.0f, 1.0f);
    const float cx = a.x + dx * t;
    const float cy = a.y + dy * t;
    const float ox = p.x - cx;
    const float oy = p.y - cy;
    return (ox * ox + oy * oy) <= tolerance * tolerance;
}

} // namespace

// A world-space box for picking and framing. Uses the collider when there is
// one, then the sprite's extent, and finally a small fixed box - so a bare
// transform is still clickable rather than being an entity you can see in the
// Hierarchy and never select in the viewport.
eng::AABB ScenePanel::EntityWorldBounds(eng::Entity& entity) {
    if (auto* box = entity.Find<eng::AABBColliderComponent>(); box != nullptr) {
        return box->WorldBounds();
    }
    if (auto* circle = entity.Find<eng::CircleColliderComponent>(); circle != nullptr) {
        return circle->WorldBounds();
    }

    const eng::Vec2 position = entity.Transform().WorldPosition();

    if (auto* sprite = entity.Find<eng::SpriteComponent>(); sprite != nullptr) {
        eng::Vec2 size = sprite->PixelSize();
        if (size.x <= 0.0f || size.y <= 0.0f) {
            if (const eng::Texture* texture =
                    eng::ResourceManager::Get(sprite->GetTexture());
                texture != nullptr) {
                size = eng::Vec2{static_cast<eng::f32>(texture->width),
                                 static_cast<eng::f32>(texture->height)};
            }
        }
        const eng::Vec2 scale = entity.Transform().WorldScale();
        const eng::Vec2 half{std::abs(size.x * scale.x) * 0.5f,
                             std::abs(size.y * scale.y) * 0.5f};
        if (half.x > 0.0f && half.y > 0.0f) {
            return eng::AABB::FromCenterHalfExtents(position, half);
        }
    }

    return eng::AABB::FromCenterHalfExtents(position, eng::Vec2{16.0f, 16.0f});
}

void ScenePanel::Draw() {
    // The target is sized from the CONTENT REGION and resized BEFORE the image
    // is submitted. Resizing afterwards would destroy the texture whose id was
    // just recorded into the draw list, and the frame would sample freed
    // memory.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    m_target.Resize(static_cast<eng::i32>(avail.x), static_cast<eng::i32>(avail.y));

    m_imageOrigin = ImGui::GetCursorScreenPos();
    m_imageSize   = ImVec2(static_cast<float>(m_target.Width()),
                           static_cast<float>(m_target.Height()));

    if (m_target.IsValid()) {
        ImGui::Image(reinterpret_cast<ImTextureID>(m_target.NativeTexture()), m_imageSize);
    } else {
        ImGui::TextDisabled("no render target");
        return;
    }

    // ---- DROP TARGET, on the image itself --------------------------------
    //
    // Immediately after ImGui::Image, because BeginDragDropTarget applies to
    // the LAST SUBMITTED ITEM. Moving this below the gizmo code would silently
    // target whatever was submitted last instead, and drops would land on the
    // wrong thing or nowhere.
    //
    // The drop POSITION is the cursor's world position, not the camera centre:
    // dragging a texture to a particular spot and having it appear somewhere
    // else is the kind of thing that makes a tool feel like it is not
    // listening.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadTexture)) {
            const auto* path = static_cast<const char*>(payload->Data);
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const eng::Vec2 world = m_camera.ScreenToWorld(
                eng::Vec2{mouse.x - m_imageOrigin.x, mouse.y - m_imageOrigin.y});

            std::string message;
            const eng::EntityHandle created = CreateEntityForAsset(path, world, message);
            if (!created.IsNull()) {
                // SELECTED on creation, so the gizmo is already on the thing
                // that was just dropped and it can be nudged without hunting
                // for it in the Hierarchy.
                EditorState::Get().selected = created;
            }
            std::snprintf(m_status, sizeof(m_status), "%s", message.c_str());
            ENGINE_LOG_INFO(eng::Channels::kEditor, "scene drop: {}", message);
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadScene)) {
            // DEFERRED through EditorState rather than loaded here - loading
            // destroys every entity, and this is the middle of a frame in
            // which other panels are holding on to them.
            EditorState::Get().requestedScene = static_cast<const char*>(payload->Data);
        }
        ImGui::EndDragDropTarget();
    }

    m_hovered = ImGui::IsItemHovered();

    // The camera's viewport has to match the image for the mouse maths below to
    // agree with what is on screen. RenderView sets it again from the bound
    // target; setting it here as well means picking is correct on the very
    // first frame, before anything has rendered.
    m_camera.SetViewportSize(eng::Vec2{m_imageSize.x, m_imageSize.y});

    // ORDER IS LOAD-BEARING. The gizmo gets first refusal on the click, then
    // picking runs only if the gizmo did not take it, then the gizmo is drawn.
    // See the note above UpdateGizmo for the bug the other order produced.
    UpdateGizmo(m_imageOrigin);
    HandlePicking(m_imageOrigin, m_imageSize);
    DrawGizmo(m_imageOrigin);

    // A compact status line over the image, rather than a row that steals
    // height from the view.
    ImGui::SetCursorScreenPos(ImVec2(m_imageOrigin.x + 8.0f, m_imageOrigin.y + 6.0f));
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.75f, 0.78f, 0.85f, 0.9f), "zoom %.2fx   (%.0f, %.0f)",
                       static_cast<double>(m_camera.Zoom()),
                       static_cast<double>(m_camera.Position().x),
                       static_cast<double>(m_camera.Position().y));

    // THE SCREENTOWORLD TEST, live, and this is the only place it can honestly
    // live now. It used to be in the Viewport panel reading the raw WINDOW
    // mouse position, which was correct while the world filled the window and
    // became nonsense the moment it moved into a panel at an offset. Here the
    // number is next to the cursor that produced it, so a wrong inverse
    // transform is visible immediately rather than being a pair of digits in
    // another panel that nobody is looking at.
    if (m_hovered) {
        const ImVec2    mouse = ImGui::GetIO().MousePos;
        const eng::Vec2 world = m_camera.ScreenToWorld(
            eng::Vec2{mouse.x - m_imageOrigin.x, mouse.y - m_imageOrigin.y});
        ImGui::TextColored(ImVec4(0.75f, 0.78f, 0.85f, 0.9f), "cursor %.1f, %.1f",
                           static_cast<double>(world.x), static_cast<double>(world.y));
    }
    if (eng::Engine::Get().IsInPlayMode()) {
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.30f, 0.95f),
                           "PLAY MODE - edits here are reverted on Stop");
    }
    if (m_status[0] != '\0') {
        ImGui::TextColored(ImVec4(0.65f, 0.88f, 0.70f, 0.95f), "%s", m_status);
    }
    ImGui::EndGroup();
}

void ScenePanel::HandlePicking(const ImVec2& imageOrigin, const ImVec2& imageSize) {
    (void)imageSize;
    ImGuiIO& io = ImGui::GetIO();

    if (!m_hovered && m_dragAxis == GizmoAxis::None) {
        return;
    }

    // --- pan: middle or right drag ---------------------------------------
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
        ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        const ImVec2 delta = io.MouseDelta;
        // Divided by zoom so a drag moves the world under the cursor by the
        // same number of PIXELS regardless of how far in you are. The y is
        // negated because the camera flips it - see Camera::ViewMatrix.
        m_camera.Move(eng::Vec2{-delta.x / m_camera.Zoom(), delta.y / m_camera.Zoom()});
    }

    // --- zoom: wheel, toward the cursor ----------------------------------
    if (m_hovered && io.MouseWheel != 0.0f) {
        const eng::Vec2 screen{io.MousePos.x - imageOrigin.x, io.MousePos.y - imageOrigin.y};
        const eng::Vec2 worldBefore = m_camera.ScreenToWorld(screen);

        m_camera.SetZoom(m_camera.Zoom() * std::pow(1.15f, io.MouseWheel));

        // Put the point that was under the cursor back under the cursor. This
        // is the entire difference between zoom that feels right and zoom you
        // have to correct with a pan every time.
        const eng::Vec2 worldAfter = m_camera.ScreenToWorld(screen);
        m_camera.Move(worldBefore - worldAfter);
    }

    // --- F frames the selection ------------------------------------------
    if (m_hovered && ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.WantTextInput) {
        FrameSelection();
    }

    // --- left click selects ----------------------------------------------
    // Only when a gizmo drag is not in progress, and only on the press rather
    // than on the release, so click-and-drag on a handle never also reselects.
    if (m_hovered && m_dragAxis == GizmoAxis::None &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const eng::Vec2 screen{io.MousePos.x - imageOrigin.x, io.MousePos.y - imageOrigin.y};
        const eng::Vec2 world = m_camera.ScreenToWorld(screen);

        eng::Scene&       scene = eng::Engine::Get().GetScene();
        eng::EntityHandle hit;
        eng::f32          bestArea = 0.0f;

        scene.ForEach([&](eng::Entity& entity) {
            const eng::AABB bounds = EntityWorldBounds(entity);
            if (!eng::Contains(bounds, world)) {
                return;
            }
            // SMALLEST wins. Clicking a moon that sits inside its planet's
            // bounds should select the moon, and picking the first hit would
            // select whichever happened to come first in the slot array.
            const eng::Vec2 size = bounds.Size();
            const eng::f32  area = std::max(size.x * size.y, 0.0001f);
            if (hit.IsNull() || area < bestArea) {
                hit      = entity.Handle();
                bestArea = area;
            }
        });

        // Clicking empty space clears the selection, which is what every editor
        // does and what makes "deselect" discoverable without a shortcut.
        EditorState::Get().selected = hit;
    }
}

// THE INPUT HALF OF THE GIZMO, and it MUST run before HandlePicking.
//
// The first version did the opposite and the gizmo did not work at all. The
// order was picking, then gizmo, and picking's guard read `m_dragAxis ==
// None` - which is set by the gizmo, LATER IN THE SAME FRAME. So on the one
// frame that matters, the frame of the press, the guard was always open:
// clicking the X handle - which sits sixty pixels away from the object, over
// empty space - ran a pick there, cleared the selection, and the gizmo it was
// about to grab no longer had an entity. Nothing moved, and nothing looked
// broken enough to point at.
//
// The lesson is not "call these in the other order". It is that a guard
// against state produced later in the same frame is not a guard. Splitting
// input from drawing makes the ordering requirement explicit rather than
// implicit in where a draw call happened to sit.
void ScenePanel::UpdateGizmo(const ImVec2& imageOrigin) {
    eng::Scene&  scene    = eng::Engine::Get().GetScene();
    eng::Entity* selected = scene.Get(EditorState::Get().selected);
    if (selected == nullptr) {
        m_dragAxis = GizmoAxis::None;
        return;
    }

    ImGuiIO&        io    = ImGui::GetIO();
    const eng::Vec2 world = selected->Transform().WorldPosition();
    const GizmoScreen g   = GizmoScreenFor(m_camera, imageOrigin, world);

    // --- grab -------------------------------------------------------------
    if (m_dragAxis == GizmoAxis::None && m_hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse   = io.MousePos;
        GizmoAxis    grabbed = GizmoAxis::None;

        // Centre first: it overlaps the root of both arms, and testing an arm
        // first would make the centre square unreachable.
        if (std::abs(mouse.x - g.origin.x) <= kCentreHalf &&
            std::abs(mouse.y - g.origin.y) <= kCentreHalf) {
            grabbed = GizmoAxis::Both;
        } else if (NearSegment(mouse, g.origin, g.xEnd, kHandleRadius)) {
            grabbed = GizmoAxis::X;
        } else if (NearSegment(mouse, g.origin, g.yEnd, kHandleRadius)) {
            grabbed = GizmoAxis::Y;
        }

        if (grabbed != GizmoAxis::None) {
            m_dragAxis = grabbed;
            // The grab OFFSET, so the entity does not snap its centre to the
            // cursor the instant you touch a handle.
            const eng::Vec2 mouseScreen{mouse.x - imageOrigin.x, mouse.y - imageOrigin.y};
            m_dragGrabOffset = world - m_camera.ScreenToWorld(mouseScreen);
        }
    }

    // --- drag -------------------------------------------------------------
    if (m_dragAxis == GizmoAxis::None) {
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_dragAxis = GizmoAxis::None;
        return;
    }

    const eng::Vec2 mouseScreen{io.MousePos.x - imageOrigin.x,
                                io.MousePos.y - imageOrigin.y};
    const eng::Vec2 target = m_camera.ScreenToWorld(mouseScreen) + m_dragGrabOffset;

    // ONE AXIS AT A TIME. The unconstrained axis keeps the value it had, so
    // dragging the X arm slides the object horizontally however far the cursor
    // wanders vertically - which is the entire reason an axis handle exists
    // rather than just the centre square.
    eng::Vec2 next = world;
    if (m_dragAxis == GizmoAxis::X || m_dragAxis == GizmoAxis::Both) {
        next.x = target.x;
    }
    if (m_dragAxis == GizmoAxis::Y || m_dragAxis == GizmoAxis::Both) {
        next.y = target.y;
    }

    // SetWorldPosition, not SetLocalPosition. Dragging a child of a rotated
    // parent has to land where the cursor is, which means going through the
    // parent's inverse - and that is exactly what SetWorldPosition does.
    if (!eng::ApproxEqual(next, world, 0.0001f)) {
        selected->Transform().SetWorldPosition(next);
        EditorState::Get().dirty = true;
    }
}

// The drawing half. Pure output - it reads m_dragAxis to colour the centre
// square and changes nothing.
void ScenePanel::DrawGizmo(const ImVec2& imageOrigin) {
    eng::Scene&  scene    = eng::Engine::Get().GetScene();
    eng::Entity* selected = scene.Get(EditorState::Get().selected);
    if (selected == nullptr) {
        return;
    }

    ImDrawList*       draw = ImGui::GetWindowDrawList();
    const GizmoScreen g =
        GizmoScreenFor(m_camera, imageOrigin, selected->Transform().WorldPosition());

    const bool  dragging = m_dragAxis != GizmoAxis::None;
    const ImU32 red      = IM_COL32(230, 80, 70, 255);
    const ImU32 green    = IM_COL32(110, 210, 100, 255);
    const ImU32 yellow   = IM_COL32(240, 215, 90, 255);

    // The axis being dragged is drawn thicker, so there is never any doubt
    // about which constraint is in force.
    const float xWidth = (m_dragAxis == GizmoAxis::X) ? 4.0f : 2.5f;
    const float yWidth = (m_dragAxis == GizmoAxis::Y) ? 4.0f : 2.5f;

    draw->AddLine(g.origin, g.xEnd, red, xWidth);
    draw->AddLine(g.origin, g.yEnd, green, yWidth);
    draw->AddCircleFilled(g.xEnd, kHandleRadius, red);
    draw->AddCircleFilled(g.yEnd, kHandleRadius, green);
    draw->AddRectFilled(ImVec2(g.origin.x - kCentreHalf, g.origin.y - kCentreHalf),
                        ImVec2(g.origin.x + kCentreHalf, g.origin.y + kCentreHalf),
                        (m_dragAxis == GizmoAxis::Both) ? yellow : IM_COL32(240, 215, 90, 140));
}

void ScenePanel::FrameSelection() {
    eng::Scene&  scene    = eng::Engine::Get().GetScene();
    eng::Entity* selected = scene.Get(EditorState::Get().selected);
    if (selected == nullptr) {
        return;
    }
    const eng::AABB bounds = EntityWorldBounds(*selected);
    m_camera.SetPosition(bounds.Center());

    const eng::Vec2 size = bounds.Size();
    const eng::Vec2 view = m_camera.ViewportSize();
    if (size.x > 0.0f && size.y > 0.0f && view.x > 0.0f && view.y > 0.0f) {
        // Fit with margin, and never zoom IN past 1:1 - framing a 32-pixel
        // sprite should not leave you at 20x staring at four texels.
        const eng::f32 fit = std::min(view.x / (size.x * 4.0f), view.y / (size.y * 4.0f));
        m_camera.SetZoom(std::min(fit, 1.0f));
    }
}

void ScenePanel::RenderView() {
    if (!m_target.IsValid()) {
        return;
    }

    eng::Renderer::SetRenderTarget(&m_target);

    // The editing conveniences live here rather than in the engine's render
    // path, so they appear in the Scene view and never in the Game view - which
    // is the entire distinction between the two panels.
    eng::DebugDraw::Grid(100.0f, eng::Color{42, 42, 52, 255});
    eng::DebugDraw::OriginAxes(120.0f);

    eng::Engine::Get().RenderWorld(m_camera, /*includeDebugDraw=*/true);

    eng::Renderer::SetRenderTarget(nullptr);
}

} // namespace editor
