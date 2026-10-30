// ============================================================================
//  ScenePanel.cpp - the Scene view. See ScenePanel.h for the controls and for
//  why the editor camera is separate from the game camera.
// ============================================================================

#include "panels/ScenePanel.h"

#include "AssetDragDrop.h"
#include "EditorApp.h"
#include "panels/TilePalettePanel.h"

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
    //
    // WHATEVER SHAPE THE COLLIDER IS. This used to ask for a box and then for
    // a circle by name, which was complete when those were the only two - and
    // would have quietly made a capsule, polygon or edge collider impossible
    // to click on, because neither lookup would match and it would fall
    // through to the sprite.
    if (auto* collider = eng::FindCollider(entity); collider != nullptr) {
        return collider->WorldBounds();
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
        // THE CAMERA ENTITY, when there is one. Writing straight to the
        // engine's Camera would be undone on the very next frame, because
        // CameraSystem copies the active camera entity into it - so the
        // button would appear to do nothing at all. See CameraComponent.h.
        if (eng::CameraComponent* active = eng::CameraSystem::Active()) {
            if (eng::Transform2D* transform = active->OwnerTransform()) {
                transform->SetWorldPosition(m_camera.Position());
            }
            active->SetZoom(m_camera.Zoom());
        } else {
            // No camera entity: the old path, which still works and is what
            // a scene written before cameras were entities uses.
            eng::Camera& gameCamera = eng::Engine::Get().GetCamera();
            gameCamera.SetPosition(m_camera.Position());
            gameCamera.SetZoom(m_camera.Zoom());
        }
        EditorState::Get().MarkDirty();
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
                EditorState::Get().SelectOnly(created);
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

    // THE ORDER OF THESE MATTERS. See the long note above UpdateGizmo.
    //
    // Painting comes first of all: while a brush is chosen the mouse belongs
    // to it, and the handles and picking must not also act on the same click.
    const bool painting = HandleTilePainting(m_imageOrigin);
    if (!painting) {
        UpdateGizmo(m_imageOrigin);
    }
    HandlePicking(m_imageOrigin, painting);
    if (!painting) {
        DrawGizmo(m_imageOrigin);
    }

    DrawTileOverlay(m_imageOrigin);
    DrawOverlay();
}

// ---------------------------------------------------------------------------
//  Tile painting
// ---------------------------------------------------------------------------

namespace {

// The connected run of cells holding the same thing as the one clicked.
//
// BOUNDED, and it has to be. A tile grid has no edges, so flood-filling empty
// space would walk outward until it ran out of memory. The painted area plus
// a one-cell margin is the region Unity's own tilemap fill uses, and it is the
// one that makes "fill the inside of this room" do what you meant.
std::vector<eng::TileCoord> FloodRegion(const eng::TilemapComponent& map,
                                        eng::TileCoord from, std::size_t limit) {
    std::vector<eng::TileCoord> filled;

    eng::TileCoord lo{};
    eng::TileCoord hi{};
    if (!map.TileBounds(lo, hi)) {
        // Nothing painted at all, so there is no region to speak of and no
        // sensible place to stop. One cell is the honest answer.
        filled.push_back(from);
        return filled;
    }

    lo.x -= 1; lo.y -= 1;
    hi.x += 1; hi.y += 1;

    const bool insideBounds = (from.x >= lo.x) && (from.x <= hi.x) &&
                              (from.y >= lo.y) && (from.y <= hi.y);
    if (!insideBounds) {
        filled.push_back(from);
        return filled;
    }

    const eng::TileId target = map.GetTile(from);

    const int width  = hi.x - lo.x + 1;
    const int height = hi.y - lo.y + 1;
    std::vector<bool> seen(static_cast<std::size_t>(width) * height, false);

    const auto index = [&](eng::TileCoord cell) {
        return static_cast<std::size_t>(cell.y - lo.y) * width +
               static_cast<std::size_t>(cell.x - lo.x);
    };

    std::vector<eng::TileCoord> pending{from};
    seen[index(from)] = true;

    while (!pending.empty() && filled.size() < limit) {
        const eng::TileCoord cell = pending.back();
        pending.pop_back();
        filled.push_back(cell);

        // Four-way and not eight. A diagonal leak would pour through the
        // single-pixel gap where two walls meet at a corner, which is exactly
        // where a level has them.
        const eng::TileCoord steps[4] = {
            {cell.x + 1, cell.y}, {cell.x - 1, cell.y},
            {cell.x, cell.y + 1}, {cell.x, cell.y - 1},
        };
        for (const eng::TileCoord& next : steps) {
            if (next.x < lo.x || next.x > hi.x || next.y < lo.y || next.y > hi.y) {
                continue;
            }
            if (seen[index(next)]) {
                continue;
            }
            if (map.GetTile(next) != target) {
                continue;
            }
            seen[index(next)] = true;
            pending.push_back(next);
        }
    }

    return filled;
}

} // namespace

bool ScenePanel::HandleTilePainting(const ImVec2& imageOrigin) {
    m_hasHoveredCell = false;

    TilePaintState& state = TilePaintState::Get();
    if (!state.IsPainting()) {
        return false;
    }

    eng::TilemapComponent* map = state.TargetMap();
    if (map == nullptr) {
        // A brush with nothing to paint into. The editor is left behaving
        // normally rather than swallowing clicks that can do nothing - the
        // Tile Palette already says there is no target.
        return false;
    }

    if (!m_hovered) {
        return true;   // the mode is on; the pointer is simply somewhere else
    }

    const ImGuiIO&  io    = ImGui::GetIO();
    const eng::Vec2 world = m_camera.ScreenToWorld(
        eng::Vec2{io.MousePos.x - imageOrigin.x, io.MousePos.y - imageOrigin.y});

    m_hoveredCell    = map->WorldToCell(world);
    m_hasHoveredCell = true;

    // Middle-drag still pans, so there is always a way to move around without
    // leaving the brush. Right-drag is taken over for erasing.
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        return true;
    }

    const bool leftDown  = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);

    // Resolved only when something is actually about to be painted. Doing it
    // on hover would add a tile to the map's palette just for moving the mouse
    // across the view.
    const auto brushId = [&]() -> eng::TileId {
        return state.tilePath.empty() ? eng::kEmptyTile
                                      : map->AddToPalette(state.tilePath);
    };

    const auto markDirty = [] { EditorState::Get().MarkDirty(); };

    switch (state.tool) {
        case TileTool::Select:
            break;

        case TileTool::Paint: {
            if (leftDown) {
                if (const eng::TileId id = brushId(); id != eng::kEmptyTile) {
                    map->SetTile(m_hoveredCell, id);
                    markDirty();
                }
            } else if (rightDown) {
                map->SetTile(m_hoveredCell, eng::kEmptyTile);
                markDirty();
            }
            break;
        }

        case TileTool::Erase: {
            if (leftDown || rightDown) {
                map->SetTile(m_hoveredCell, eng::kEmptyTile);
                markDirty();
            }
            break;
        }

        case TileTool::Rectangle: {
            const bool leftPressed  = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            const bool rightPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Right);

            if (!state.dragging && (leftPressed || rightPressed)) {
                state.dragging  = true;
                state.dragErase = rightPressed;
                state.dragStart = m_hoveredCell;
            } else if (state.dragging && !leftDown && !rightDown) {
                // Applied ON RELEASE, so the box can be adjusted before it is
                // committed - and so a mis-started drag can be taken back by
                // dragging it down to nothing.
                const eng::TileId id = state.dragErase ? eng::kEmptyTile : brushId();
                if (state.dragErase || id != eng::kEmptyTile) {
                    const int minX = std::min(state.dragStart.x, m_hoveredCell.x);
                    const int maxX = std::max(state.dragStart.x, m_hoveredCell.x);
                    const int minY = std::min(state.dragStart.y, m_hoveredCell.y);
                    const int maxY = std::max(state.dragStart.y, m_hoveredCell.y);

                    for (int y = minY; y <= maxY; ++y) {
                        for (int x = minX; x <= maxX; ++x) {
                            map->SetTile(x, y, id);
                        }
                    }
                    markDirty();
                    std::snprintf(m_status, sizeof(m_status), "%s %d x %d cells",
                                  state.dragErase ? "cleared" : "filled",
                                  maxX - minX + 1, maxY - minY + 1);
                }
                state.dragging = false;
            }
            break;
        }

        case TileTool::Fill: {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                const bool erasing = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
                const eng::TileId id = erasing ? eng::kEmptyTile : brushId();

                if (erasing || id != eng::kEmptyTile) {
                    constexpr std::size_t kFillLimit = 200000;
                    const std::vector<eng::TileCoord> region =
                        FloodRegion(*map, m_hoveredCell, kFillLimit);

                    for (const eng::TileCoord& cell : region) {
                        map->SetTile(cell, id);
                    }
                    markDirty();

                    if (region.size() >= kFillLimit) {
                        std::snprintf(m_status, sizeof(m_status),
                                      "fill stopped at %zu cells", region.size());
                    } else {
                        std::snprintf(m_status, sizeof(m_status), "filled %zu cell(s)",
                                      region.size());
                    }
                }
            }
            break;
        }

        case TileTool::Pick: {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const eng::TileId under = map->GetTile(m_hoveredCell);
                if (under == eng::kEmptyTile) {
                    std::snprintf(m_status, sizeof(m_status), "that cell is empty");
                } else {
                    state.tilePath = map->Palette()[static_cast<std::size_t>(under) - 1];
                    state.tool     = TileTool::Paint;
                    std::snprintf(m_status, sizeof(m_status), "picked up '%s'",
                                  state.tilePath.c_str());
                }
            }
            break;
        }
    }

    return true;
}

void ScenePanel::CellCornersOnScreen(const ImVec2& imageOrigin,
                                     const eng::TilemapComponent& map, int x, int y,
                                     ImVec2 out[4]) const {
    const eng::AABB local = map.CellBoundsLocal(x, y);

    eng::Mat3 world = eng::Mat3::Identity();
    if (const eng::Transform2D* transform = map.OwnerTransform(); transform != nullptr) {
        world = transform->WorldMatrix();
    }

    // Anticlockwise from the bottom-left, so the four points form a loop that
    // can be handed straight to AddPolyline.
    const eng::Vec2 corners[4] = {
        local.min,
        eng::Vec2{local.max.x, local.min.y},
        local.max,
        eng::Vec2{local.min.x, local.max.y},
    };

    for (int i = 0; i < 4; ++i) {
        const eng::Vec2 screen =
            m_camera.WorldToScreen(world.TransformPoint(corners[i]));
        out[i] = ImVec2(imageOrigin.x + screen.x, imageOrigin.y + screen.y);
    }
}

void ScenePanel::DrawTileOverlay(const ImVec2& imageOrigin) {
    TilePaintState& state = TilePaintState::Get();
    if (!state.IsPainting()) {
        return;
    }

    eng::TilemapComponent* map = state.TargetMap();
    if (map == nullptr) {
        return;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Clipped to the picture, or the grid would be drawn over the panels
    // docked beside this one.
    draw->PushClipRect(imageOrigin,
                       ImVec2(imageOrigin.x + m_imageSize.x,
                              imageOrigin.y + m_imageSize.y),
                       true);

    // ---- the grid ---------------------------------------------------------
    if (state.showGrid) {
        const eng::Vec2 cellSize = map->CellSize();
        const eng::AABB visible  = m_camera.VisibleBounds();

        eng::Mat3 world = eng::Mat3::Identity();
        if (const eng::Transform2D* transform = map->OwnerTransform();
            transform != nullptr) {
            world = transform->WorldMatrix();
        }
        const eng::Mat3 toLocal = world.Inverse();

        // All four corners, because a rotated map's visible area is a tilted
        // rectangle in its own space - the same argument the renderer makes.
        const eng::Vec2 screenCorners[4] = {
            visible.min,
            eng::Vec2{visible.max.x, visible.min.y},
            visible.max,
            eng::Vec2{visible.min.x, visible.max.y},
        };
        eng::Vec2 lo = toLocal.TransformPoint(screenCorners[0]);
        eng::Vec2 hi = lo;
        for (int i = 1; i < 4; ++i) {
            const eng::Vec2 p = toLocal.TransformPoint(screenCorners[i]);
            lo.x = std::min(lo.x, p.x);
            lo.y = std::min(lo.y, p.y);
            hi.x = std::max(hi.x, p.x);
            hi.y = std::max(hi.y, p.y);
        }

        const int minX = static_cast<int>(std::floor(lo.x / cellSize.x));
        const int maxX = static_cast<int>(std::floor(hi.x / cellSize.x)) + 1;
        const int minY = static_cast<int>(std::floor(lo.y / cellSize.y));
        const int maxY = static_cast<int>(std::floor(hi.y / cellSize.y)) + 1;

        // Zoomed far enough out, the lines are closer together than they are
        // wide and the grid becomes a grey wash that hides the map underneath
        // it. Past that point it is more useful not drawn.
        constexpr int kMaxGridLines = 160;
        if ((maxX - minX) <= kMaxGridLines && (maxY - minY) <= kMaxGridLines) {
            const ImU32 colour = IM_COL32(150, 160, 185, 60);

            for (int x = minX; x <= maxX; ++x) {
                const eng::Vec2 a = world.TransformPoint(
                    eng::Vec2{static_cast<float>(x) * cellSize.x, lo.y});
                const eng::Vec2 b = world.TransformPoint(
                    eng::Vec2{static_cast<float>(x) * cellSize.x, hi.y});
                const eng::Vec2 sa = m_camera.WorldToScreen(a);
                const eng::Vec2 sb = m_camera.WorldToScreen(b);
                draw->AddLine(ImVec2(imageOrigin.x + sa.x, imageOrigin.y + sa.y),
                              ImVec2(imageOrigin.x + sb.x, imageOrigin.y + sb.y),
                              colour);
            }
            for (int y = minY; y <= maxY; ++y) {
                const eng::Vec2 a = world.TransformPoint(
                    eng::Vec2{lo.x, static_cast<float>(y) * cellSize.y});
                const eng::Vec2 b = world.TransformPoint(
                    eng::Vec2{hi.x, static_cast<float>(y) * cellSize.y});
                const eng::Vec2 sa = m_camera.WorldToScreen(a);
                const eng::Vec2 sb = m_camera.WorldToScreen(b);
                draw->AddLine(ImVec2(imageOrigin.x + sa.x, imageOrigin.y + sa.y),
                              ImVec2(imageOrigin.x + sb.x, imageOrigin.y + sb.y),
                              colour);
            }
        }
    }

    // ---- what the next click will affect ----------------------------------
    if (m_hasHoveredCell) {
        const bool erasing = (state.tool == TileTool::Erase) ||
                             (state.dragging && state.dragErase);
        const ImU32 colour = erasing ? IM_COL32(235, 90, 80, 230)
                                     : IM_COL32(250, 200, 90, 230);

        int minX = m_hoveredCell.x;
        int maxX = m_hoveredCell.x;
        int minY = m_hoveredCell.y;
        int maxY = m_hoveredCell.y;

        if (state.tool == TileTool::Rectangle && state.dragging) {
            minX = std::min(state.dragStart.x, m_hoveredCell.x);
            maxX = std::max(state.dragStart.x, m_hoveredCell.x);
            minY = std::min(state.dragStart.y, m_hoveredCell.y);
            maxY = std::max(state.dragStart.y, m_hoveredCell.y);
        }

        // The outline of the whole affected box, drawn from its corner cells
        // rather than cell by cell - a 200x200 rectangle preview would
        // otherwise be forty thousand outlines.
        ImVec2 lowCorners[4];
        ImVec2 highCorners[4];
        CellCornersOnScreen(imageOrigin, *map, minX, minY, lowCorners);
        CellCornersOnScreen(imageOrigin, *map, maxX, maxY, highCorners);

        const ImVec2 loop[4] = {
            lowCorners[0],                                  // bottom-left of min cell
            ImVec2(highCorners[1].x, lowCorners[1].y),      // bottom-right
            highCorners[2],                                 // top-right of max cell
            ImVec2(lowCorners[3].x, highCorners[3].y),      // top-left
        };
        draw->AddPolyline(loop, 4, colour, ImDrawFlags_Closed, 2.0f);

        if (minX != maxX || minY != maxY) {
            const ImVec2 label(std::min(loop[0].x, loop[2].x) + 4.0f,
                               std::min(loop[0].y, loop[2].y) - 18.0f);
            char text[64];
            std::snprintf(text, sizeof(text), "%d x %d", maxX - minX + 1,
                          maxY - minY + 1);
            draw->AddText(label, colour, text);
        }
    }

    draw->PopClipRect();

    // The mode, said out loud over the view. A brush that is on without
    // anything saying so is a brush you forget about and then paint with by
    // accident while trying to select something.
    ImGui::SetCursorScreenPos(ImVec2(imageOrigin.x + 8.0f,
                                     imageOrigin.y + m_imageSize.y - 24.0f));
    ImGui::TextColored(ImVec4(0.98f, 0.80f, 0.35f, 1.0f),
                       "TILE %s%s%s", TileToolName(state.tool),
                       state.tilePath.empty() ? "" : "  -  ",
                       state.tilePath.empty() ? "(no tile chosen)"
                                              : state.tilePath.c_str());
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

void ScenePanel::HandlePicking(const ImVec2& imageOrigin, bool painting) {
    ImGuiIO& io = ImGui::GetIO();

    if (!m_hovered && m_dragAxis == GizmoAxis::None) {
        return;
    }

    // ---- pan: drag with the middle or right button ------------------------
    //
    // WHILE A BRUSH IS CHOSEN, RIGHT-DRAG ERASES INSTEAD. Middle-drag still
    // pans, so there is always a way to move around without putting the brush
    // down - which there would not be if painting took both of them.
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
        (!painting && ImGui::IsMouseDragging(ImGuiMouseButton_Right))) {
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

    // ---- Delete destroys the selection -------------------------------------
    //
    // FOCUSED rather than merely hovered, which is deliberately stricter than
    // the F above. Framing the camera is harmless, so reacting to a mouse
    // that happens to be passing over costs nothing; destroying an entity is
    // not, and should take a click into this view first.
    //
    // Through the deferred queue, like every other delete - this runs while
    // panels are still drawing and still describing the thing being removed.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {

        // THE WHOLE SELECTION, matching the Hierarchy - and now literally the
        // same code rather than a second copy of it. See DestroySelection in
        // EditorApp.h for why that mattered.
        DestroySelection();
    }

    // ---- left click selects ------------------------------------------------
    // Only when a handle is not being dragged, and on the press rather than
    // the release, so click-and-drag on a handle never also reselects.
    if (m_hovered && !painting && m_dragAxis == GizmoAxis::None &&
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

        // THE SAME MODIFIERS THE HIERARCHY USES, so the two views do not
        // need to be learned separately. There is no Shift-range here
        // because "everything between" has no meaning in a 2D view - Shift
        // adds, which is the next most useful thing.
        EditorState&   state = EditorState::Get();
        const ImGuiIO& io    = ImGui::GetIO();

        if (hit.IsNull()) {
            // Clicking empty space clears the selection, which is what every
            // editor does and makes "deselect" discoverable without a
            // shortcut - unless a modifier is held, where clearing what
            // somebody is part way through building would be infuriating.
            if (!io.KeyCtrl && !io.KeyShift) {
                state.ClearSelection();
            }
        } else if (io.KeyCtrl) {
            state.ToggleSelection(hit);
        } else if (io.KeyShift) {
            state.AddToSelection(hit);
        } else if (!state.IsSelected(hit)) {
            state.SelectOnly(hit);
        } else {
            // Already selected, so this is somebody aiming the gizmo at a
            // different member of the set rather than starting again.
            state.selected = hit;
        }
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
    if (eng::ApproxEqual(next, world, 0.0001f)) {
        return;
    }

    // ---- BULK MOVE ---------------------------------------------------------
    //
    // Everything selected moves BY THE SAME AMOUNT the active entity moved.
    // By a delta rather than to a position, because moving them all to the
    // cursor would pile them on top of each other and destroy the
    // arrangement that is usually the reason for selecting them together.
    const eng::Vec2 delta = next - world;
    EditorState&    state = EditorState::Get();

    selected->Transform().SetWorldPosition(next);

    for (const eng::EntityId id : state.selection) {
        if (id == state.selected) {
            continue;   // the active one has already moved, above
        }
        eng::Entity* other = scene.Get(id);
        if (other == nullptr) {
            continue;
        }

        // SKIP ANYTHING WHOSE ANCESTOR IS ALSO MOVING, or it moves twice.
        // A child follows its parent on its own - that is what a parent is -
        // so adding the delta again would send it off at double speed. The
        // same reasoning as the reparent in HierarchyPanel.cpp.
        bool ancestorMoving = false;
        for (const eng::EntityId candidate : state.selection) {
            if (candidate == id) {
                continue;
            }
            eng::Entity* ancestor = scene.Get(candidate);
            if (ancestor != nullptr &&
                other->Transform().IsDescendantOf(&ancestor->Transform())) {
                ancestorMoving = true;
                break;
            }
        }
        if (ancestorMoving) {
            continue;
        }

        other->Transform().SetWorldPosition(other->Transform().WorldPosition() + delta);
    }

    EditorState::Get().MarkDirty();
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
    // ======================================================================
    //  THE AREA THE CAMERA CAN SEE, worked out here because this panel is
    //  the only thing that knows both the camera and the size of the picture
    //  it is drawing into. Without it the grid was a fixed box around the
    //  world origin and simply stopped once you panned past its edge.
    //
    //  The two opposite CORNERS of the view are converted from screen pixels
    //  back into world units. Screen y runs downward and world y runs up, so
    //  the top-left of the screen is the HIGHEST point in the world - which
    //  is why the two results are sorted into a min and a max rather than
    //  used in the order they come out.
    //
    //  Padded by one step so the lines reach past the edges instead of
    //  stopping a few pixels short of them.
    // ======================================================================
    constexpr float kGridSpacing = 100.0f;

    const eng::Vec2 corner0 = m_camera.ScreenToWorld(eng::Vec2{0.0f, 0.0f});
    const eng::Vec2 corner1 =
        m_camera.ScreenToWorld(eng::Vec2{m_imageSize.x, m_imageSize.y});

    const eng::AABB view = eng::AABB::FromMinMax(
        eng::Vec2{std::min(corner0.x, corner1.x) - kGridSpacing,
                  std::min(corner0.y, corner1.y) - kGridSpacing},
        eng::Vec2{std::max(corner0.x, corner1.x) + kGridSpacing,
                  std::max(corner0.y, corner1.y) + kGridSpacing});

    eng::Gizmos::Grid(view, kGridSpacing, eng::Color{42, 42, 52, 255});
    eng::Gizmos::OriginAxes(120.0f);

    // ======================================================================
    //  WHAT THE GAME CAMERA FRAMES, outlined in the Scene view.
    //
    //  This is most of the point of making the camera an entity: you can see
    //  where the shot is without switching to the Game view and guessing.
    //  The live one is drawn brightly and the others dimly, so a scene with
    //  two cameras shows at a glance which is which.
    //
    //  Measured against the GAME view's size, not this panel's - the box has
    //  to show what a player would see, and the two panels are rarely the
    //  same shape.
    // ======================================================================
    eng::Scene& scene = eng::Engine::Get().GetScene();
    const eng::Vec2 gameViewport = eng::Engine::Get().GetCamera().ViewportSize();

    if (gameViewport.x > 0.0f && gameViewport.y > 0.0f) {
        scene.ForEach([&](eng::Entity& entity) {
            const auto* camera = entity.Find<eng::CameraComponent>();
            if (camera == nullptr) {
                return;
            }

            const bool live = camera->IsActive();
            const eng::Color colour =
                live ? eng::Color{250, 210, 90, 255} : eng::Color{120, 120, 130, 160};

            eng::Gizmos::Box(camera->WorldBounds(gameViewport), colour, 0.0f,
                             eng::GizmoSpace::World, eng::GizmoCategory::Bounds);

            eng::Gizmos::Text(camera->WorldBounds(gameViewport).min +
                                  eng::Vec2{4.0f, 4.0f},
                              live ? entity.Name() + " (live)" : entity.Name(), colour,
                              0.0f, eng::GizmoSpace::World,
                              eng::GizmoCategory::Bounds);
        });
    }

    eng::Engine::Get().RenderWorld(m_camera, /*includeGizmos=*/true,
                                   /*editorTints=*/true);

    eng::Renderer::SetRenderTarget(nullptr);
}

} // namespace editor
