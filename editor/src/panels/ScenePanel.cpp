// ============================================================================
//  ScenePanel.cpp - the Scene view. See ScenePanel.h for the controls and for
//  why the editor camera is separate from the game camera.
// ============================================================================

#include "panels/ScenePanel.h"

#include "AssetDragDrop.h"
#include "EditorApp.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace editor {
namespace {

// The move handles are a fixed size in SCREEN pixels, not in world units.
// That is the whole reason they are drawn with ImGui rather than as gizmos: a
// handle that shrank as you zoomed out would become unusable exactly when you
// most needed it.
constexpr float kAxisLength   = 60.0f;
constexpr float kHandleRadius = 7.0f;
constexpr float kCentreHalf   = 9.0f;

ImVec2 Add(const ImVec2& a, const eng::Vec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }

// Where the three handles are on screen.
//
// Worked out in ONE place and used by both the input pass and the drawing
// pass. If those two ever computed it separately they would eventually
// disagree, and the result is a handle you can see and cannot grab.
struct GizmoScreen {
    ImVec2 origin;
    ImVec2 xEnd;
    ImVec2 yEnd;
};

GizmoScreen GizmoScreenFor(const eng::Camera& camera, const ImVec2& imageOrigin,
                           const eng::Vec2& world) {
    GizmoScreen g;
    g.origin = Add(imageOrigin, camera.WorldToScreen(world));

    // +x is to the right; +y is UP the screen, which in screen pixels means
    // SUBTRACTING - the camera flipped the axis on the way in, and the handles
    // have to agree with what is drawn underneath them.
    g.xEnd = ImVec2(g.origin.x + kAxisLength, g.origin.y);
    g.yEnd = ImVec2(g.origin.x, g.origin.y - kAxisLength);
    return g;
}

// Is point `p` within `tolerance` pixels of the line from `a` to `b`?
// Used to decide whether a click landed on one of the arms.
bool NearSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b, float tolerance) {
    const float dx       = b.x - a.x;
    const float dy       = b.y - a.y;
    const float lengthSq = dx * dx + dy * dy;
    if (lengthSq <= 0.0001f) {
        return false;
    }
    // How far along the line the nearest point is, as a fraction from 0 to 1.
    float t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSq;
    t = std::clamp(t, 0.0f, 1.0f);

    const float cx = a.x + dx * t;
    const float cy = a.y + dy * t;
    const float ox = p.x - cx;
    const float oy = p.y - cy;
    return (ox * ox + oy * oy) <= tolerance * tolerance;
}

} // namespace

eng::AABB ScenePanel::EntityWorldBounds(eng::Entity& entity) {
    // A box for clicking on and for framing. It uses the collider if there is
    // one, then the sprite's size, and finally a small fixed box - so an
    // entity with nothing but a transform is still clickable rather than being
    // something you can see in the Hierarchy and never select in the view.
    if (auto* box = entity.Find<eng::AABBColliderComponent>(); box != nullptr) {
        return box->WorldBounds();
    }
    if (auto* circle = entity.Find<eng::CircleColliderComponent>(); circle != nullptr) {
        return circle->WorldBounds();
    }

    const eng::Vec2 position = entity.Transform().WorldPosition();

    if (auto* sprite = entity.Find<eng::SpriteComponent>(); sprite != nullptr) {
        eng::Vec2 size = sprite->PixelSize();
        if ((size.x <= 0.0f || size.y <= 0.0f) && sprite->GetTexture()) {
            size = eng::Vec2{static_cast<float>(sprite->GetTexture()->width),
                             static_cast<float>(sprite->GetTexture()->height)};
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
    // ---- the small toolbar across the top --------------------------------
    if (ImGui::Button("Gizmos")) {
        ImGui::OpenPopup("gizmomenu");
    }
    if (ImGui::BeginPopup("gizmomenu")) {
        // The same idea as Unity's Gizmos dropdown: switch groups of helper
        // shapes on and off without touching the code that draws them.
        bool enabled = eng::Gizmos::IsEnabled();
        if (ImGui::MenuItem("Show gizmos", nullptr, &enabled)) {
            eng::Gizmos::SetEnabled(enabled);
        }
        ImGui::Separator();
        for (int i = 0; i < static_cast<int>(eng::GizmoCategory::Count); ++i) {
            const auto category = static_cast<eng::GizmoCategory>(i);
            bool       on       = eng::Gizmos::IsCategoryEnabled(category);
            if (ImGui::MenuItem(eng::ToString(category), nullptr, &on)) {
                eng::Gizmos::SetCategoryEnabled(category, on);
            }
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset view")) {
        m_camera.Reset();
    }
    ImGui::SameLine();
    if (ImGui::Button("Frame selection")) {
        FrameSelection();
    }

    ImGui::SameLine();
    if (ImGui::Button("Align game camera")) {
        // Points the GAME camera at whatever this view is looking at - the
        // same idea as Unity's "Align With View". Framing a shot here and then
        // saving is how a scene file gets its starting camera, and without
        // this there would be no way to change it from inside the editor.
        eng::Camera& gameCamera = eng::Engine::Get().GetCamera();
        gameCamera.SetPosition(m_camera.Position());
        gameCamera.SetZoom(m_camera.Zoom());
        EditorState::Get().dirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Move the game camera to where this view is looking. Saved "
                          "with the scene.");
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(drag with the middle or right button to pan, wheel to zoom, F "
                        "to frame)");

    // ---- the view itself --------------------------------------------------
    //
    // The picture is resized BEFORE it is submitted for display. Resizing it
    // afterwards would throw away the very texture that was just recorded for
    // drawing, and the frame would try to display memory that had been freed.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    m_target.Resize(static_cast<int>(avail.x), static_cast<int>(avail.y));

    m_imageOrigin = ImGui::GetCursorScreenPos();
    m_imageSize   = ImVec2(static_cast<float>(m_target.Width()),
                           static_cast<float>(m_target.Height()));

    if (!m_target.IsValid()) {
        ImGui::TextDisabled("this view could not be created");
        return;
    }
    ImGui::Image(reinterpret_cast<ImTextureID>(m_target.NativeTexture()), m_imageSize);

    // ---- accepting a dropped file ----------------------------------------
    //
    // Immediately after ImGui::Image, because a drop target attaches to the
    // most recently drawn item. Putting this after the handle code below would
    // silently target something else.
    //
    // The new entity appears where the CURSOR is, not at the centre of the
    // view. Dragging a texture to a particular spot and having it appear
    // somewhere else makes a tool feel like it is not listening.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadTexture)) {
            const auto*     path  = static_cast<const char*>(payload->Data);
            const ImVec2    mouse = ImGui::GetIO().MousePos;
            const eng::Vec2 world = m_camera.ScreenToWorld(
                eng::Vec2{mouse.x - m_imageOrigin.x, mouse.y - m_imageOrigin.y});

            std::string        message;
            const eng::EntityId created = CreateEntityForAsset(path, world, message);
            if (!created.IsNull()) {
                // Selected straight away, so the move handles are already on
                // the thing that was just dropped.
                EditorState::Get().selected = created;
            }
            std::snprintf(m_status, sizeof(m_status), "%s", message.c_str());
            ENGINE_LOG_INFO(eng::Channels::kEditor, "{}", message);
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadScene)) {
            // Not loaded here. Loading a scene destroys every entity, and this
            // is the middle of a frame in which other panels are still holding
            // on to them - so the request is recorded and acted on later. See
            // EditorState::requestedScene.
            EditorState::Get().requestedScene = static_cast<const char*>(payload->Data);
        }
        ImGui::EndDragDropTarget();
    }

    m_hovered = ImGui::IsItemHovered();

    // The camera's viewport has to match the picture, or the mouse arithmetic
    // below would disagree with what is on screen.
    m_camera.SetViewportSize(eng::Vec2{m_imageSize.x, m_imageSize.y});

    // THE ORDER OF THESE THREE MATTERS. See the long note above UpdateGizmo.
    UpdateGizmo(m_imageOrigin);
    HandlePicking(m_imageOrigin);
    DrawGizmo(m_imageOrigin);

    DrawOverlay();
}

void ScenePanel::DrawOverlay() {
    // A few lines of text over the top-left of the picture, rather than a row
    // of widgets that would steal height from the view.
    ImGui::SetCursorScreenPos(ImVec2(m_imageOrigin.x + 8.0f, m_imageOrigin.y + 6.0f));
    ImGui::BeginGroup();

    ImGui::TextColored(ImVec4(0.75f, 0.78f, 0.85f, 0.9f), "zoom %.2fx   (%.0f, %.0f)",
                       static_cast<double>(m_camera.Zoom()),
                       static_cast<double>(m_camera.Position().x),
                       static_cast<double>(m_camera.Position().y));

    // The world position under the cursor. Useful constantly while placing
    // things, and it is also a live check that the camera's screen-to-world
    // conversion is right: if the numbers do not match where the cursor
    // visibly is, something in Camera is wrong.
    if (m_hovered) {
        const ImVec2    mouse = ImGui::GetIO().MousePos;
        const eng::Vec2 world = m_camera.ScreenToWorld(
            eng::Vec2{mouse.x - m_imageOrigin.x, mouse.y - m_imageOrigin.y});
        ImGui::TextColored(ImVec4(0.75f, 0.78f, 0.85f, 0.9f), "cursor %.1f, %.1f",
                           static_cast<double>(world.x), static_cast<double>(world.y));
    }

    if (eng::Engine::Get().IsInPlayMode()) {
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.30f, 0.95f),
                           "PLAY MODE - anything edited here is undone when you press Stop");
    }
    if (m_status[0] != '\0') {
        ImGui::TextColored(ImVec4(0.65f, 0.88f, 0.70f, 0.95f), "%s", m_status);
    }
    ImGui::EndGroup();
}

void ScenePanel::HandlePicking(const ImVec2& imageOrigin) {
    ImGuiIO& io = ImGui::GetIO();

    if (!m_hovered && m_dragAxis == GizmoAxis::None) {
        return;
    }

    // ---- pan: drag with the middle or right button ------------------------
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
        ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        const ImVec2 delta = io.MouseDelta;
        // Divided by the zoom so a drag moves the world under the cursor by
        // the same number of PIXELS however far in you are. The y is negated
        // because the camera flips that axis.
        m_camera.Move(eng::Vec2{-delta.x / m_camera.Zoom(), delta.y / m_camera.Zoom()});
    }

    // ---- zoom: the wheel, towards the cursor ------------------------------
    if (m_hovered && io.MouseWheel != 0.0f) {
        const eng::Vec2 screen{io.MousePos.x - imageOrigin.x,
                               io.MousePos.y - imageOrigin.y};

        // Where the cursor was pointing BEFORE the zoom...
        const eng::Vec2 worldBefore = m_camera.ScreenToWorld(screen);

        m_camera.SetZoom(m_camera.Zoom() * std::pow(1.15f, io.MouseWheel));

        // ...and where it points now. Moving the camera by the difference puts
        // the same point back under the cursor. That is the entire difference
        // between zoom that feels right and zoom you have to correct with a
        // pan every single time.
        const eng::Vec2 worldAfter = m_camera.ScreenToWorld(screen);
        m_camera.Move(worldBefore - worldAfter);
    }

    // ---- F frames the selection -------------------------------------------
    // WantTextInput is checked so that typing an F into a text box somewhere
    // else does not also move the camera.
    if (m_hovered && ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.WantTextInput) {
        FrameSelection();
    }

    // ---- left click selects ------------------------------------------------
    // Only when a handle is not being dragged, and on the press rather than
    // the release, so click-and-drag on a handle never also reselects.
    if (m_hovered && m_dragAxis == GizmoAxis::None &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const eng::Vec2 screen{io.MousePos.x - imageOrigin.x,
                               io.MousePos.y - imageOrigin.y};
        const eng::Vec2 world = m_camera.ScreenToWorld(screen);

        eng::Scene&   scene = eng::Engine::Get().GetScene();
        eng::EntityId hit;
        float         bestArea = 0.0f;

        scene.ForEach([&](eng::Entity& entity) {
            const eng::AABB bounds = EntityWorldBounds(entity);
            if (!eng::Contains(bounds, world)) {
                return;
            }
            // The SMALLEST thing under the cursor wins. Clicking a moon that
            // sits inside its planet's bounds should select the moon; taking
            // the first match instead would select whichever happened to come
            // first in the scene's list.
            const eng::Vec2 size = bounds.Size();
            const float     area = std::max(size.x * size.y, 0.0001f);
            if (hit.IsNull() || area < bestArea) {
                hit      = entity.Id();
                bestArea = area;
            }
        });

        // Clicking empty space clears the selection, which is what every
        // editor does and makes "deselect" discoverable without a shortcut.
        EditorState::Get().selected = hit;
    }
}

// ============================================================================
//  THE INPUT HALF OF THE MOVE HANDLES, and it MUST run before HandlePicking.
//
//  The first version had it the other way round and the handles did not work
//  at all. Picking's guard reads `m_dragAxis == None` - and m_dragAxis is set
//  by this function, LATER IN THE SAME FRAME. So on the one frame that
//  matters, the frame of the click, the guard was always open: clicking the X
//  handle, which sits sixty pixels away from the object over empty space, ran
//  a pick there, cleared the selection, and the handle it was about to grab no
//  longer had anything to move. Nothing happened, and nothing looked broken
//  enough to point at.
//
//  The lesson is not "call them the other way round". It is that a guard
//  against something produced later in the same frame is not a guard at all.
// ============================================================================
void ScenePanel::UpdateGizmo(const ImVec2& imageOrigin) {
    eng::Scene&  scene    = eng::Engine::Get().GetScene();
    eng::Entity* selected = scene.Get(EditorState::Get().selected);
    if (selected == nullptr) {
        m_dragAxis = GizmoAxis::None;
        return;
    }

    ImGuiIO&          io    = ImGui::GetIO();
    const eng::Vec2   world = selected->Transform().WorldPosition();
    const GizmoScreen g     = GizmoScreenFor(m_camera, imageOrigin, world);

    // ---- grabbing a handle -------------------------------------------------
    if (m_dragAxis == GizmoAxis::None && m_hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse   = io.MousePos;
        GizmoAxis    grabbed = GizmoAxis::None;

        // The centre square is tested FIRST, because it overlaps the base of
        // both arms - testing an arm first would make the centre unreachable.
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
            // Remember how far the object was from the cursor when it was
            // grabbed, so it does not snap its centre to the cursor the
            // instant you touch a handle.
            const eng::Vec2 mouseScreen{mouse.x - imageOrigin.x, mouse.y - imageOrigin.y};
            m_dragGrabOffset = world - m_camera.ScreenToWorld(mouseScreen);
        }
    }

    // ---- dragging ----------------------------------------------------------
    if (m_dragAxis == GizmoAxis::None) {
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_dragAxis = GizmoAxis::None;   // let go
        return;
    }

    const eng::Vec2 mouseScreen{io.MousePos.x - imageOrigin.x,
                                io.MousePos.y - imageOrigin.y};
    const eng::Vec2 target = m_camera.ScreenToWorld(mouseScreen) + m_dragGrabOffset;

    // ONE AXIS AT A TIME. The axis that was not grabbed keeps the value it
    // had, so dragging the red arm slides the object horizontally however far
    // the cursor wanders up and down - which is the entire reason to have
    // separate arms rather than just the centre square.
    eng::Vec2 next = world;
    if (m_dragAxis == GizmoAxis::X || m_dragAxis == GizmoAxis::Both) {
        next.x = target.x;
    }
    if (m_dragAxis == GizmoAxis::Y || m_dragAxis == GizmoAxis::Both) {
        next.y = target.y;
    }

    // SetWorldPosition rather than SetLocalPosition. Dragging a child of a
    // rotated parent has to land where the cursor is, and working out what
    // local position that corresponds to is exactly what SetWorldPosition does.
    if (!eng::ApproxEqual(next, world, 0.0001f)) {
        selected->Transform().SetWorldPosition(next);
        EditorState::Get().dirty = true;
    }
}

void ScenePanel::DrawGizmo(const ImVec2& imageOrigin) {
    // Purely output: this reads m_dragAxis to decide what to highlight and
    // changes nothing.
    eng::Scene&  scene    = eng::Engine::Get().GetScene();
    eng::Entity* selected = scene.Get(EditorState::Get().selected);
    if (selected == nullptr) {
        return;
    }

    ImDrawList*       draw = ImGui::GetWindowDrawList();
    const GizmoScreen g =
        GizmoScreenFor(m_camera, imageOrigin, selected->Transform().WorldPosition());

    // Red for x and green for y, matching the origin arrows in the world and
    // the convention every 3D tool uses.
    const ImU32 red    = IM_COL32(230, 80, 70, 255);
    const ImU32 green  = IM_COL32(110, 210, 100, 255);
    const ImU32 yellow = IM_COL32(240, 215, 90, 255);

    // The arm being dragged is drawn thicker, so there is never any doubt
    // about which direction the movement is locked to.
    const float xWidth = (m_dragAxis == GizmoAxis::X) ? 4.0f : 2.5f;
    const float yWidth = (m_dragAxis == GizmoAxis::Y) ? 4.0f : 2.5f;

    draw->AddLine(g.origin, g.xEnd, red, xWidth);
    draw->AddLine(g.origin, g.yEnd, green, yWidth);
    draw->AddCircleFilled(g.xEnd, kHandleRadius, red);
    draw->AddCircleFilled(g.yEnd, kHandleRadius, green);
    draw->AddRectFilled(
        ImVec2(g.origin.x - kCentreHalf, g.origin.y - kCentreHalf),
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
        // Fit it with some room around it, and never zoom IN past actual size -
        // framing a 32-pixel sprite should not leave you at 20x staring at four
        // enormous pixels.
        const float fit = std::min(view.x / (size.x * 4.0f), view.y / (size.y * 4.0f));
        m_camera.SetZoom(std::min(fit, 1.0f));
    }
}

void ScenePanel::RenderView() {
    if (!m_target.IsValid()) {
        return;
    }

    eng::Renderer::SetRenderTarget(&m_target);

    // The editing helpers are queued HERE rather than inside the engine, so
    // they appear in the Scene view and never in the Game view. That is the
    // entire difference between the two panels.
    eng::Gizmos::Grid(100.0f, eng::Color{42, 42, 52, 255});
    eng::Gizmos::OriginAxes(120.0f);

    eng::Engine::Get().RenderWorld(m_camera, /*includeGizmos=*/true);

    eng::Renderer::SetRenderTarget(nullptr);
}

} // namespace editor
