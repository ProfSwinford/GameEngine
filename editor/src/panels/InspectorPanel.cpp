// ============================================================================
//  InspectorPanel.cpp - the Inspector. See InspectorPanel.h for the three
//  answers about editing while the game is running.
//
//  Each component type gets its own small draw function below. Adding a new
//  one means writing another and adding a line to the chain in
//  InspectorPanel::Draw - editor work, which touches no engine code at all.
// ============================================================================

#include "panels/InspectorPanel.h"

#include "AssetDragDrop.h"
#include "EditorApp.h"
#include "ShellOpen.h"

#include <imgui.h>
#include <imgui_internal.h>   // GetCurrentWindow, for a whole-window drop target

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace editor {
namespace {

// ---------------------------------------------------------------------------
//  TransformComponent
// ---------------------------------------------------------------------------
void DrawTransform(eng::TransformComponent& component, bool running) {
    eng::Transform2D& transform = component.Transform();

    // Answer 2 from the header: while the game is running, a system may own
    // these values. They are shown but not editable, and the reason is written
    // on screen rather than left as a mystery.
    ImGui::BeginDisabled(running);

    // ImGui's DragFloat widgets want a plain array of floats, so the Vec2 is
    // unpacked into one, edited, and packed back if it changed. Each of these
    // returns true only on a frame the value was actually changed.
    float position[2] = {transform.LocalPosition().x, transform.LocalPosition().y};
    if (ImGui::DragFloat2("Position", position, 0.5f)) {
        transform.SetLocalPosition(eng::Vec2{position[0], position[1]});
        EditorState::Get().dirty = true;
    }

    // Shown in DEGREES, stored in radians. Every angle a person sees in this
    // editor is degrees; every angle the engine holds is radians. The
    // conversion happens here, at the edge, exactly once.
    float degrees = transform.LocalRotation() * eng::kRadToDeg;
    if (ImGui::DragFloat("Rotation", &degrees, 0.5f, -360.0f, 360.0f, "%.1f deg")) {
        transform.SetLocalRotation(degrees * eng::kDegToRad);
        EditorState::Get().dirty = true;
    }

    float scale[2] = {transform.LocalScale().x, transform.LocalScale().y};
    if (ImGui::DragFloat2("Scale", scale, 0.01f)) {
        transform.SetLocalScale(eng::Vec2{scale[0], scale[1]});
        EditorState::Get().dirty = true;
    }

    ImGui::EndDisabled();
    if (running) {
        ImGui::TextDisabled("read-only while playing - a system may own these. "
                            "Pause to edit.");
    }

    // The LOCAL values are above; the WORLD position is shown as well, because
    // for a child entity those two are different and the difference is exactly
    // what confuses people about parenting.
    const eng::Vec2 world = transform.WorldPosition();
    ImGui::TextDisabled("world position: %.2f, %.2f   (%d parent(s) above it)",
                        static_cast<double>(world.x), static_cast<double>(world.y),
                        transform.Depth());
}

// ---------------------------------------------------------------------------
//  SpriteComponent
// ---------------------------------------------------------------------------
void DrawSprite(eng::SpriteComponent& sprite) {
    char path[192];
    std::snprintf(path, sizeof(path), "%s", sprite.TexturePath().c_str());
    if (ImGui::InputText("Texture", path, sizeof(path),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        sprite.SetTexture(path);
        EditorState::Get().dirty = true;
    }
    ImGui::TextDisabled("press Enter to load, or drag an image from the Assets panel");

    // ImGui's colour picker works in 0-1 floats; the engine stores 0-255
    // bytes, because that is what an image editor shows you. Converted here.
    const eng::Color tint = sprite.Tint();
    float rgba[4] = {tint.r / 255.0f, tint.g / 255.0f, tint.b / 255.0f, tint.a / 255.0f};
    if (ImGui::ColorEdit4("Tint", rgba)) {
        sprite.SetTint(eng::Color{static_cast<unsigned char>(rgba[0] * 255.0f),
                                  static_cast<unsigned char>(rgba[1] * 255.0f),
                                  static_cast<unsigned char>(rgba[2] * 255.0f),
                                  static_cast<unsigned char>(rgba[3] * 255.0f)});
        EditorState::Get().dirty = true;
    }

    int layer = sprite.Layer();
    if (ImGui::DragInt("Draw order", &layer, 0.2f)) {
        sprite.SetLayer(layer);
        EditorState::Get().dirty = true;
    }
    ImGui::TextDisabled("higher numbers are drawn on top");

    // A preview of the actual image, plus how many things are sharing it.
    if (const eng::TextureRef& texture = sprite.GetTexture(); texture) {
        ImGui::Image(reinterpret_cast<ImTextureID>(texture->native), ImVec2(64, 64));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("%d x %d pixels", texture->width, texture->height);

        // use_count() is how many shared_ptrs are keeping this image loaded -
        // one per sprite using it, plus this preview. It is the clearest
        // possible demonstration of what shared ownership means.
        ImGui::TextDisabled("shared by %ld thing(s)",
                            static_cast<long>(texture.use_count()));
        if (texture->isPlaceholder) {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                               "this image failed to load");
        }
        ImGui::EndGroup();
    }
}

// ---------------------------------------------------------------------------
//  Colliders
// ---------------------------------------------------------------------------

// Collects every layer name mentioned anywhere in the scene, plus a few common
// ones, so the checkbox list below offers something sensible without anybody
// having to register layer names in advance.
//
// std::set keeps them sorted and refuses duplicates, which is exactly what is
// wanted here.
std::set<std::string> GatherLayerNames(eng::Scene& scene) {
    std::set<std::string> names{"Default", "Player", "Enemy", "Pickup", "World"};

    scene.ForEach([&](eng::Entity& entity) {
        entity.ForEachComponent([&](eng::Component& component) {
            if (auto* collider = dynamic_cast<eng::ColliderComponent*>(&component)) {
                names.insert(collider->Layer());
                for (const std::string& other : collider->CollidesWith()) {
                    if (other != eng::kCollisionLayerAll) {
                        names.insert(other);
                    }
                }
            }
        });
    });
    return names;
}

// ---------------------------------------------------------------------------
//  The outline of a polygon or an edge collider, as an editable list.
//
//  A TEMPLATE over the two collider types rather than two near-identical
//  functions, because they offer the same three operations - Points,
//  SetPoints and a maximum - and the drift between two copies of a list editor
//  is exactly the kind nobody notices until one of them loses a point.
//
//  THE WHOLE LIST IS REPLACED on any change rather than edited in place. That
//  is what routes every edit through SetPoints, which is where the count is
//  checked and clamped - so the panel cannot produce a shape the collision
//  system refuses.
// ---------------------------------------------------------------------------
template <typename T>
void DrawPointList(T& collider, int maxPoints) {
    std::vector<eng::Vec2> points = collider.Points();
    bool                   changed = false;

    // Deferred, for the same reason the Assets panel defers a move: removing
    // an entry while the loop that draws it is still running would read past
    // the end of the list.
    int removeAt = -1;

    for (std::size_t i = 0; i < points.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));

        float xy[2] = {points[i].x, points[i].y};
        if (ImGui::DragFloat2("##point", xy, 0.5f)) {
            points[i] = eng::Vec2{xy[0], xy[1]};
            changed    = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            removeAt = static_cast<int>(i);
        }
        ImGui::PopID();
    }

    if (removeAt >= 0 && points.size() > 2) {
        points.erase(points.begin() + removeAt);
        changed = true;
    }

    if (static_cast<int>(points.size()) < maxPoints && ImGui::SmallButton("+ point")) {
        // The new point goes just past the last one rather than on top of it.
        // Two points in the same place have no direction between them, and a
        // shape with a zero-length edge is one the maths has to special-case.
        const eng::Vec2 last = points.empty() ? eng::Vec2{0.0f, 0.0f} : points.back();
        points.push_back(last + eng::Vec2{8.0f, 0.0f});
        changed = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d of %d points", static_cast<int>(points.size()), maxPoints);

    if (changed) {
        collider.SetPoints(std::move(points));
        EditorState::Get().dirty = true;
    }
}

// ---------------------------------------------------------------------------
//  Rigidbody2D
// ---------------------------------------------------------------------------
void DrawRigidbody(eng::Rigidbody2DComponent& body) {
    int type = 0;
    switch (body.BodyType()) {
        case eng::RigidbodyType::Dynamic:   type = 0; break;
        case eng::RigidbodyType::Kinematic: type = 1; break;
        case eng::RigidbodyType::Static:    type = 2; break;
    }
    if (ImGui::Combo("Body type", &type, "Dynamic\0Kinematic\0Static\0")) {
        body.SetBodyType(type == 0   ? eng::RigidbodyType::Dynamic
                         : type == 1 ? eng::RigidbodyType::Kinematic
                                     : eng::RigidbodyType::Static);
        EditorState::Get().dirty = true;
    }

    // WHAT EACH ONE MEANS, on screen rather than in a manual. Picking the
    // wrong body type is the most common physics mistake there is, and the
    // moment somebody is choosing is the moment the explanation is worth
    // having.
    switch (body.BodyType()) {
        case eng::RigidbodyType::Dynamic:
            ImGui::TextDisabled("gravity and forces move it; collisions push it about");
            break;
        case eng::RigidbodyType::Kinematic:
            ImGui::TextDisabled("moves only when told to, and pushes dynamic things "
                                "out of the way - a lift, a platform, a character");
            break;
        case eng::RigidbodyType::Static:
            ImGui::TextDisabled("never moves; the same as having no rigidbody at all");
            break;
    }

    const bool dynamic = body.BodyType() == eng::RigidbodyType::Dynamic;
    if (!dynamic) {
        ImGui::BeginDisabled();
    }

    bool autoMass = body.UseAutoMass();
    if (ImGui::Checkbox("Auto mass", &autoMass)) {
        body.SetUseAutoMass(autoMass);
        EditorState::Get().dirty = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(from the collider's area x its density)");

    if (autoMass) {
        ImGui::BeginDisabled();
    }
    float mass = body.Mass();
    if (ImGui::DragFloat("Mass", &mass, 0.1f, 0.0001f, 100000.0f)) {
        body.SetMass(mass);
        EditorState::Get().dirty = true;
    }
    if (autoMass) {
        ImGui::EndDisabled();
        ImGui::TextDisabled("working out to %.3f", static_cast<double>(body.Mass()));
    }

    float gravityScale = body.GravityScale();
    if (ImGui::DragFloat("Gravity scale", &gravityScale, 0.05f, -10.0f, 10.0f)) {
        body.SetGravityScale(gravityScale);
        EditorState::Get().dirty = true;
    }
    ImGui::TextDisabled("0 floats, 1 is normal, 2 falls twice as hard");

    float linearDrag = body.LinearDrag();
    if (ImGui::DragFloat("Linear drag", &linearDrag, 0.05f, 0.0f, 100.0f)) {
        body.SetLinearDrag(linearDrag);
        EditorState::Get().dirty = true;
    }

    float angularDrag = body.AngularDrag();
    if (ImGui::DragFloat("Angular drag", &angularDrag, 0.01f, 0.0f, 100.0f)) {
        body.SetAngularDrag(angularDrag);
        EditorState::Get().dirty = true;
    }

    bool freeze = body.FreezeRotation();
    if (ImGui::Checkbox("Freeze rotation", &freeze)) {
        body.SetFreezeRotation(freeze);
        EditorState::Get().dirty = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(nearly always what a character wants)");

    if (!dynamic) {
        ImGui::EndDisabled();
    }

    // THE LIVE VALUES, which are not settings and are not saved. Worth showing
    // because "why is it drifting?" is answered by looking at a velocity that
    // is not quite zero, and there is nowhere else to see it.
    ImGui::SeparatorText("Right now");
    const eng::Vec2 velocity = body.Velocity();
    ImGui::Text("velocity  %.2f, %.2f", static_cast<double>(velocity.x),
                static_cast<double>(velocity.y));
    ImGui::Text("spin      %.3f rad/s",
                static_cast<double>(body.AngularVelocity()));
}

// ---------------------------------------------------------------------------
//  TilemapComponent
//
//  Deliberately NOT a cell editor. Cells are painted in the Scene view with
//  the Tile Palette; a grid of numbers here would be a second, worse way of
//  doing the same thing. What belongs here is everything ABOUT the map that
//  is not a cell: how big a cell is, which layer it draws on, and which
//  materials it uses.
// ---------------------------------------------------------------------------
void DrawTilemap(eng::TilemapComponent& map) {
    ImGui::Text("%zu tile(s) in %zu chunk(s)", map.TileCount(), map.ChunkCount());

    eng::TileCoord lo{};
    eng::TileCoord hi{};
    if (map.TileBounds(lo, hi)) {
        ImGui::TextDisabled("painted from (%d, %d) to (%d, %d)", lo.x, lo.y, hi.x, hi.y);
    } else {
        ImGui::TextDisabled("nothing painted yet - use the Tile Palette");
    }

    int layer = map.Layer();
    if (ImGui::DragInt("Layer", &layer, 0.2f, -1000, 1000)) {
        map.SetLayer(layer);
        EditorState::Get().dirty = true;
    }
    ImGui::TextDisabled("shared with sprites: a NEGATIVE layer puts the map behind them");

    eng::Color    tint = map.Tint();
    float         rgba[4] = {tint.r / 255.0f, tint.g / 255.0f, tint.b / 255.0f,
                             tint.a / 255.0f};
    if (ImGui::ColorEdit4("Tint", rgba)) {
        map.SetTint(eng::Color{static_cast<unsigned char>(rgba[0] * 255.0f),
                               static_cast<unsigned char>(rgba[1] * 255.0f),
                               static_cast<unsigned char>(rgba[2] * 255.0f),
                               static_cast<unsigned char>(rgba[3] * 255.0f)});
        EditorState::Get().dirty = true;
    }

    // ---- cell size ---------------------------------------------------------
    const eng::Vec2 cellSize = map.CellSize();
    bool            explicitSize = map.HasExplicitCellSize();

    if (ImGui::Checkbox("Set cell size by hand", &explicitSize)) {
        // Unticking goes back to the tile's own size, which is what a map
        // drawn at the size its art was drawn at wants.
        map.SetCellSize(explicitSize ? cellSize : eng::Vec2{0.0f, 0.0f});
        EditorState::Get().dirty = true;
    }

    if (map.HasExplicitCellSize()) {
        float size[2] = {cellSize.x, cellSize.y};
        if (ImGui::DragFloat2("Cell size", size, 0.5f, 1.0f, 4096.0f)) {
            map.SetCellSize(eng::Vec2{size[0], size[1]});
            EditorState::Get().dirty = true;
        }
    } else {
        ImGui::Text("Cell size: %.0f x %.0f", static_cast<double>(cellSize.x),
                    static_cast<double>(cellSize.y));
        ImGui::TextDisabled("taken from the first tile in the palette");
    }

    // ---- the palette -------------------------------------------------------
    ImGui::SeparatorText("Palette");
    ImGui::TextDisabled("a cell stores a position in this list, not a path");

    if (map.Palette().empty()) {
        ImGui::TextDisabled("empty - painting adds to it");
    }

    for (std::size_t i = 0; i < map.Palette().size(); ++i) {
        const std::string&    path  = map.Palette()[i];
        const eng::TileAsset* asset = map.AssetFor(static_cast<eng::TileId>(i + 1));

        ImGui::PushID(static_cast<int>(i));
        if (asset != nullptr) {
            ImGui::Text("%zu. %s", i + 1, path.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%dpx%s%s)", asset->TileSize(),
                                asset->IsSolid() ? ", solid" : "",
                                asset->IsConnector() ? ", connector" : "");
        } else {
            // Named but not loadable. Said in red, because every cell using
            // this entry is occupied as far as the map is concerned and
            // invisible as far as the screen is - which is a confusing pair of
            // symptoms to be left to work out.
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                               "%zu. %s  - WILL NOT LOAD", i + 1, path.c_str());
        }
        ImGui::PopID();
    }

    if (!map.Palette().empty()) {
        if (ImGui::Button("Drop unused entries")) {
            const std::size_t dropped = map.CompactPalette();
            EditorState::Get().dirty  = true;
            ENGINE_LOG_INFO(eng::Channels::kTilemap,
                            "tilemap palette compacted: {} entr{} dropped", dropped,
                            dropped == 1 ? "y" : "ies");
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload tiles")) {
            // Re-reads every .otile the map uses, so editing one in a text
            // editor and coming back shows the change.
            for (const std::string& path : map.Palette()) {
                std::string error;
                if (!eng::TileLibrary::Reload(path, error)) {
                    ENGINE_LOG_ERROR(eng::Channels::kTilemap,
                                     "could not reload '{}': {}", path, error);
                }
            }
            map.RefreshPalette();
        }
    }

    ImGui::SeparatorText("Danger");
    if (ImGui::Button("Clear every cell")) {
        ImGui::OpenPopup("confirmclear");
    }
    if (ImGui::BeginPopupModal("confirmclear", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Erase all %zu tile(s) in this map?", map.TileCount());
        ImGui::TextDisabled("There is no undo.");
        if (ImGui::Button("Erase")) {
            map.Clear();
            EditorState::Get().dirty = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void DrawCollider(eng::ColliderComponent& collider, eng::Scene& scene) {
    bool enabled = collider.Enabled();
    if (ImGui::Checkbox("Enabled", &enabled)) {
        collider.SetEnabled(enabled);
        EditorState::Get().dirty = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(switched off it takes part in nothing - no events, no "
                        "pushing, invisible to queries)");

    bool trigger = collider.IsTrigger();
    if (ImGui::Checkbox("Trigger", &trigger)) {
        collider.SetTrigger(trigger);
        EditorState::Get().dirty = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(reports overlaps but does not block movement)");

    // SPELLED OUT, because this is the one setting that silently changes which
    // hook a script needs. See physics/Collider.h.
    if (trigger) {
        ImGui::TextDisabled("sends OnTriggerEnter / Stay / Exit - NOT OnCollision...");
    } else {
        ImGui::TextDisabled("sends OnCollisionEnter / Stay / Exit");
    }

    float offset[2] = {collider.Offset().x, collider.Offset().y};
    if (ImGui::DragFloat2("Offset", offset, 0.5f)) {
        collider.SetOffset(eng::Vec2{offset[0], offset[1]});
        EditorState::Get().dirty = true;
    }

// The shape-specific fields. Which ones depend on what this actually is.
    switch (collider.Shape()) {
        case eng::ColliderShape::Box: {
            auto& box = static_cast<eng::BoxColliderComponent&>(collider);
            float half[2] = {box.HalfExtents().x, box.HalfExtents().y};
            if (ImGui::DragFloat2("Half size", half, 0.5f, 0.0f, 10000.0f)) {
                box.SetHalfExtents(eng::Vec2{half[0], half[1]});
                EditorState::Get().dirty = true;
            }
            ImGui::TextDisabled("half the width and half the height");

            float edge = box.EdgeRadius();
            if (ImGui::DragFloat("Edge radius", &edge, 0.1f, 0.0f, 1000.0f)) {
                box.SetEdgeRadius(edge);
                EditorState::Get().dirty = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(rounds the corners)");
            break;
        }

        case eng::ColliderShape::Circle: {
            auto& circle = static_cast<eng::CircleColliderComponent&>(collider);
            float radius = circle.Radius();
            if (ImGui::DragFloat("Radius", &radius, 0.5f, 0.0f, 10000.0f)) {
                circle.SetRadius(radius);
                EditorState::Get().dirty = true;
            }
            break;
        }

        case eng::ColliderShape::Capsule: {
            auto& capsule = static_cast<eng::CapsuleColliderComponent&>(collider);
            float size[2] = {capsule.Size().x, capsule.Size().y};
            if (ImGui::DragFloat2("Size", size, 0.5f, 0.0f, 10000.0f)) {
                capsule.SetSize(eng::Vec2{size[0], size[1]});
                EditorState::Get().dirty = true;
            }

            int direction = capsule.Direction() == eng::CapsuleDirection::Vertical ? 0 : 1;
            if (ImGui::Combo("Direction", &direction, "Vertical\0Horizontal\0")) {
                capsule.SetDirection(direction == 0 ? eng::CapsuleDirection::Vertical
                                                    : eng::CapsuleDirection::Horizontal);
                EditorState::Get().dirty = true;
            }
            ImGui::TextDisabled("the round ends are on the long axis; as wide as it is "
                                "tall makes a circle");
            break;
        }

        case eng::ColliderShape::Polygon: {
            auto& polygon = static_cast<eng::PolygonColliderComponent&>(collider);
            DrawPointList(polygon, eng::ConvexShape::kMaxPoints);

            // SAID OUT LOUD, because a dented outline still collides - as its
            // convex hull - and the shape on screen then differs from the
            // shape being tested with nothing to show why.
            if (!polygon.IsConvex()) {
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f),
                                   "NOT CONVEX - this collides as the hull of these "
                                   "points, not as drawn.");
                ImGui::TextDisabled("Split it into several colliders instead.");
            }
            break;
        }

        case eng::ColliderShape::Tilemap: {
            auto& tiles = static_cast<eng::TilemapColliderComponent&>(collider);

            ImGui::TextWrapped(
                "This collider has no shape of its own: it is the TilemapComponent "
                "beside it. Cells whose tile says \"collision\": \"Grid\" are solid, "
                "and the rest are walked through.");

            const std::size_t cells = tiles.SolidCellCount();
            const std::size_t boxes = tiles.BoxCount();
            if (boxes > 0) {
                ImGui::Text("%zu solid cell(s) merged into %zu box(es)", cells, boxes);
                ImGui::TextDisabled(
                    "merged %.0f to 1 - a flat floor is ONE box, so nothing can catch "
                    "on a join between two of them",
                    static_cast<double>(cells) / static_cast<double>(boxes));
            } else if (cells > 0) {
                ImGui::TextDisabled("%zu solid cell(s), not yet merged", cells);
            } else {
                ImGui::TextDisabled("nothing here is solid yet");
            }

            if (ImGui::Button("Rebuild")) {
                // Only needed when a .otile has been edited: changing a tile
                // from decoration to solid changes which cells are solid
                // without any cell having changed, so the map's revision does
                // not move and nothing would otherwise notice.
                tiles.Invalidate();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Painting rebuilds this automatically. Use it after "
                                  "editing a .otile file's \"collision\" setting.");
            }
            break;
        }

        case eng::ColliderShape::Edge: {
            auto& edge = static_cast<eng::EdgeColliderComponent&>(collider);
            DrawPointList(edge, eng::EdgeColliderComponent::kMaxEdgePoints);

            float radius = edge.EdgeRadius();
            if (ImGui::DragFloat("Edge radius", &radius, 0.1f, 0.0f, 1000.0f)) {
                edge.SetEdgeRadius(radius);
                EditorState::Get().dirty = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(gives the line thickness)");

            bool loop = edge.Loop();
            if (ImGui::Checkbox("Loop", &loop)) {
                edge.SetLoop(loop);
                EditorState::Get().dirty = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(joins the last point back to the first)");
            break;
        }
    }

    ImGui::SeparatorText("Surface");

    eng::PhysicsMaterial2D material = collider.Material();
    bool                   materialChanged = false;

    materialChanged |= ImGui::DragFloat("Friction", &material.friction, 0.01f, 0.0f, 2.0f);
    ImGui::SameLine();
    ImGui::TextDisabled("(0 is ice)");

    materialChanged |=
        ImGui::DragFloat("Bounciness", &material.bounciness, 0.01f, 0.0f, 1.0f);
    ImGui::SameLine();
    ImGui::TextDisabled("(0 is a beanbag, 1 a superball)");

    if (materialChanged) {
        collider.SetMaterial(material);
        EditorState::Get().dirty = true;
    }

    float density = collider.Density();
    if (ImGui::DragFloat("Density", &density, 0.05f, 0.0001f, 1000.0f)) {
        collider.SetDensity(density);
        EditorState::Get().dirty = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(only used when a rigidbody has Auto mass on)");

    // NEITHER OF THE FIRST TWO DOES ANYTHING WITHOUT A RIGIDBODY, and a
    // slider that silently has no effect is worse than no slider. Said here
    // rather than left to be discovered.
    if (collider.AttachedRigidbody() == nullptr && !collider.IsTrigger()) {
        ImGui::TextDisabled("no Rigidbody2D on this entity, so nothing here is pushed "
                            "- friction and bounciness need something to act on.");
    }

    ImGui::SeparatorText("Layers");

    char layer[64];
    std::snprintf(layer, sizeof(layer), "%s", collider.Layer().c_str());
    if (ImGui::InputText("Layer (what it is)", layer, sizeof(layer),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        collider.SetLayer(layer);
        EditorState::Get().dirty = true;
    }

    // "Collides with" as a set of checkboxes. Untick one and the collision
    // events for that pairing stop immediately - no rebuild, no restart, which
    // makes it easy to see the layer rules actually working.
    ImGui::TextUnformatted("Collides with:");

    std::vector<std::string> wanted = collider.CollidesWith();
    bool changed = false;

    // "All" is a special entry meaning "everything". When it is ticked the
    // individual boxes are irrelevant, so they are greyed out.
    bool all = std::find(wanted.begin(), wanted.end(), eng::kCollisionLayerAll) !=
               wanted.end();
    if (ImGui::Checkbox("All", &all)) {
        wanted.clear();
        if (all) {
            wanted.emplace_back(eng::kCollisionLayerAll);
        }
        changed = true;
    }

    ImGui::BeginDisabled(all);
    int column = 0;
    for (const std::string& name : GatherLayerNames(scene)) {
        bool on = std::find(wanted.begin(), wanted.end(), name) != wanted.end();
        if (column++ % 3 != 0) {
            ImGui::SameLine();
        }
        // The hidden id after ## keeps two checkboxes with the same visible
        // label apart. See the note about widget identity in Panel.h.
        char id[96];
        std::snprintf(id, sizeof(id), "%s##collideswith", name.c_str());
        if (ImGui::Checkbox(id, &on)) {
            if (on) {
                wanted.push_back(name);
            } else {
                std::erase(wanted, name);
            }
            changed = true;
        }
    }
    ImGui::EndDisabled();

    if (changed) {
        collider.SetCollidesWith(std::move(wanted));
        EditorState::Get().dirty = true;
    }

    ImGui::TextDisabled("Two colliders are only tested when EACH one's list "
                        "includes the other's layer.");
}

// ---------------------------------------------------------------------------
//  SpinComponent
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
//  CameraComponent
// ---------------------------------------------------------------------------
void DrawCamera(eng::CameraComponent& camera) {
    // WHICH CAMERA IS ACTUALLY LIVE, said first and plainly. With more than
    // one camera in a scene, "why is the Game view not where I put this?" is
    // the question this panel exists to answer.
    if (camera.IsActive()) {
        ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.60f, 1.0f),
                           "This camera is driving the Game view.");
    } else if (!camera.Enabled()) {
        ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.78f, 1.0f),
                           "Disabled - it is ignored entirely.");
    } else if (const eng::CameraComponent* active = eng::CameraSystem::Active()) {
        const eng::Entity* owner = active->Owner();
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                           "NOT in use - '%s' has a higher priority (%d vs %d).",
                           owner != nullptr ? owner->Name().c_str() : "another camera",
                           active->Priority(), camera.Priority());
    }

    float zoom = camera.Zoom();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::DragFloat("Zoom", &zoom, 0.01f, 0.01f, 50.0f, "%.2fx")) {
        camera.SetZoom(zoom);
        EditorState::Get().dirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Bigger is closer in. This is the game's zoom - the Scene "
                          "view has its own and is not affected.");
    }

    int priority = camera.Priority();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::DragInt("Priority", &priority, 0.2f)) {
        camera.SetPriority(priority);
        EditorState::Get().dirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("With several cameras in a scene, the highest priority is "
                          "the one used. Ties go to whichever was found first.");
    }

    bool enabled = camera.Enabled();
    if (ImGui::Checkbox("Enabled", &enabled)) {
        camera.SetEnabled(enabled);
        EditorState::Get().dirty = true;
    }

    ImGui::Separator();

    // THE POSITION IS THE TRANSFORM'S, and saying so stops somebody looking
    // for a position field that deliberately does not exist.
    ImGui::TextDisabled("Its position is the Transform above - there is no separate");
    ImGui::TextDisabled("camera position. Parent it to something to make it follow.");

    if (eng::CameraSystem::Count() > 1) {
        ImGui::TextDisabled("%zu cameras in this scene.", eng::CameraSystem::Count());
    }
}

// ---------------------------------------------------------------------------
//  The interface components. See engine/include/engine/scene/UI.h.
// ---------------------------------------------------------------------------

// A colour, as four 0-255 channels. ImGui works in 0-1 floats, so this is the
// conversion both ways in one place rather than in each of the eight callers.
bool DrawColorField(const char* label, eng::Color& color) {
    float rgba[4] = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                     color.a / 255.0f};
    if (!ImGui::ColorEdit4(label, rgba, ImGuiColorEditFlags_AlphaBar)) {
        return false;
    }
    const auto channel = [](float v) {
        const float scaled = v * 255.0f + 0.5f;
        return static_cast<unsigned char>(scaled < 0.0f ? 0.0f
                                          : (scaled > 255.0f ? 255.0f : scaled));
    };
    color = eng::Color{channel(rgba[0]), channel(rgba[1]), channel(rgba[2]),
                       channel(rgba[3])};
    return true;
}

void DrawUIRect(eng::UIRectComponent& rect) {
    bool changed = false;

    // ---- which space ------------------------------------------------------
    int space = (rect.space == eng::UISpace::World) ? 1 : 0;
    if (ImGui::Combo("Space", &space, "Screen\0World\0")) {
        rect.space = (space == 1) ? eng::UISpace::World : eng::UISpace::Screen;
        changed    = true;
    }
    if (rect.space == eng::UISpace::World) {
        ImGui::TextDisabled("positioned by this entity's TRANSFORM, and it");
        ImGui::TextDisabled("scales with the camera - good for a name tag");
    } else {
        ImGui::TextDisabled("positioned in pixels from the anchor, and the");
        ImGui::TextDisabled("camera does not affect it - good for a score");
    }

    // ---- anchor, only when it means something -----------------------------
    //
    // DISABLED rather than hidden in world space, where the transform is in
    // charge and an anchor does nothing. A field that vanishes teaches
    // nothing; one that is visibly unavailable says it does not apply.
    ImGui::BeginDisabled(rect.space == eng::UISpace::World);
    int anchor = static_cast<int>(rect.anchor);
    if (ImGui::Combo("Anchor", &anchor,
                     "TopLeft\0Top\0TopRight\0Left\0Center\0Right\0"
                     "BottomLeft\0Bottom\0BottomRight\0")) {
        rect.anchor = static_cast<eng::UIAnchor>(anchor);
        changed     = true;
    }

    float offset[2] = {rect.offset.x, rect.offset.y};
    if (ImGui::DragFloat2("Offset", offset, 1.0f)) {
        rect.offset = eng::Vec2{offset[0], offset[1]};
        changed     = true;
    }
    ImGui::EndDisabled();

    float size[2] = {rect.size.x, rect.size.y};
    if (ImGui::DragFloat2("Size", size, 1.0f, 1.0f, 4096.0f)) {
        rect.size = eng::Vec2{size[0], size[1]};
        changed   = true;
    }

    float pivot[2] = {rect.pivot.x, rect.pivot.y};
    if (ImGui::DragFloat2("Pivot", pivot, 0.01f, 0.0f, 1.0f)) {
        rect.pivot = eng::Vec2{pivot[0], pivot[1]};
        changed    = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Which point of this object sits at the position above. "
                          "0,0 is its top-left corner and 0.5,0.5 its middle.");
    }

    if (ImGui::DragInt("Sort order", &rect.sortOrder, 0.1f, -100, 100)) {
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Higher draws later, so higher is in front.");
    }

    if (ImGui::Checkbox("Visible", &rect.visible)) {
        changed = true;
    }

    // Where it actually ended up, which is the fastest way to answer "why is
    // my label off the edge of the screen".
    ImGui::TextDisabled("on screen: %.0f,%.0f to %.0f,%.0f", rect.resolvedMin.x,
                        rect.resolvedMin.y, rect.resolvedMax.x, rect.resolvedMax.y);

    if (changed) {
        EditorState::Get().dirty = true;
    }
}

void DrawUIText(eng::UITextComponent& label) {
    bool changed = false;

    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), "%s", label.text.c_str());
    if (ImGui::InputTextMultiline("Text", buffer, sizeof(buffer),
                                  ImVec2(-1.0f, ImGui::GetTextLineHeight() * 3.0f))) {
        label.text = buffer;
        changed    = true;
    }

    char fontBuffer[256];
    std::snprintf(fontBuffer, sizeof(fontBuffer), "%s", label.fontPath.c_str());
    if (ImGui::InputTextWithHint("Font", "fonts/Something.ttf  (blank = built-in)",
                                 fontBuffer, sizeof(fontBuffer))) {
        label.fontPath = fontBuffer;
        changed        = true;
    }

    // DROPPING A FILE ON IT, which is how every other asset is assigned in
    // this editor - and the only discoverable way to find out that a .ttf
    // goes here at all.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadFont)) {
            label.fontPath = static_cast<const char*>(payload->Data);
            changed        = true;
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::DragFloat("Size", &label.fontSize, 0.5f, 4.0f, 256.0f, "%.0f px")) {
        changed = true;
    }

    changed |= DrawColorField("Colour", label.color);

    int align = static_cast<int>(label.align);
    if (ImGui::Combo("Align", &align, "Left\0Center\0Right\0")) {
        label.align = static_cast<eng::UIAlign>(align);
        changed     = true;
    }
    if (ImGui::Checkbox("Centre vertically", &label.middle)) {
        changed = true;
    }

    // What actually got used, which is the only way to tell a working font
    // path from one that silently fell back.
    if (const eng::FontRef& font = label.ResolvedFont()) {
        if (font->isFallback) {
            ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                               label.fontPath.empty()
                                   ? "using the built-in 8x8 font"
                                   : "that font did not load - using the built-in one");
        } else {
            ImGui::TextDisabled("%s at %.0fpx", font->path.c_str(), font->pixelHeight);
        }
    }

    if (changed) {
        EditorState::Get().dirty = true;
    }
}

void DrawUIImage(eng::UIImageComponent& image) {
    bool changed = false;

    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s", image.texturePath.c_str());
    if (ImGui::InputTextWithHint("Texture", "blank = a plain rectangle", buffer,
                                 sizeof(buffer))) {
        image.texturePath = buffer;
        changed           = true;
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadTexture)) {
            image.texturePath = static_cast<const char*>(payload->Data);
            changed           = true;
        }
        ImGui::EndDragDropTarget();
    }

    changed |= DrawColorField("Tint", image.tint);

    if (ImGui::DragFloat("Outline", &image.outlineThickness, 0.1f, 0.0f, 16.0f,
                         "%.0f")) {
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("0 fills the rectangle. Anything else draws its edge only. "
                          "Ignored when a texture is set.");
    }

    if (changed) {
        EditorState::Get().dirty = true;
    }
}

void DrawUIButton(eng::UIButtonComponent& button) {
    bool changed = false;

    if (ImGui::Checkbox("Interactable", &button.interactable)) {
        changed = true;
    }

    changed |= DrawColorField("Normal",   button.normalTint);
    changed |= DrawColorField("Hover",    button.hoverTint);
    changed |= DrawColorField("Pressed",  button.pressedTint);
    changed |= DrawColorField("Disabled", button.disabledTint);

    ImGui::Separator();

    // LIVE STATE, which turns "my button does nothing" from a guess into a
    // reading: hover it in the Game view and watch this.
    ImGui::TextDisabled("hovered: %s   pressed: %s",
                        button.IsHovered() ? "yes" : "no",
                        button.IsPressed() ? "yes" : "no");
    ImGui::TextWrapped("These colours tint the UIImage on this same entity, so a "
                       "button needs one. Read it from a script with "
                       "Owner()->Find<eng::UIButtonComponent>()->WasClicked().");

    if (changed) {
        EditorState::Get().dirty = true;
    }
}

void DrawUISlider(eng::UISliderComponent& slider) {
    bool changed = false;

    if (ImGui::DragFloat("Value", &slider.value, 0.01f, slider.minValue,
                         slider.maxValue)) {
        changed = true;
    }
    if (ImGui::DragFloat("Min", &slider.minValue, 0.1f)) { changed = true; }
    if (ImGui::DragFloat("Max", &slider.maxValue, 0.1f)) { changed = true; }
    if (ImGui::Checkbox("Interactable", &slider.interactable)) { changed = true; }

    changed |= DrawColorField("Track", slider.trackColor);
    changed |= DrawColorField("Fill",  slider.fillColor);
    changed |= DrawColorField("Knob",  slider.knobColor);

    if (slider.maxValue <= slider.minValue) {
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                           "max is not above min, so this cannot be dragged");
    }

    if (changed) {
        EditorState::Get().dirty = true;
    }
}

void DrawSpin(eng::SpinComponent& spin) {
    // Edited in degrees, stored in radians - same reason as the transform's
    // rotation field above.
    float degrees = spin.RadiansPerSecond() * eng::kRadToDeg;
    if (ImGui::DragFloat("Degrees/sec", &degrees, 1.0f, -720.0f, 720.0f, "%.1f")) {
        spin.SetRadiansPerSecond(degrees * eng::kDegToRad);
        EditorState::Get().dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("0")) {
        spin.SetRadiansPerSecond(0.0f);
        EditorState::Get().dirty = true;
    }

    ImGui::TextDisabled("negative numbers spin the other way");
    ImGui::TextWrapped("Anything parented to this entity ORBITS it, because a child's "
                       "position in the world is worked out through its parent. There "
                       "is no orbit code anywhere - drag this and watch.");
}

// ---------------------------------------------------------------------------
//  ScriptComponent
// ---------------------------------------------------------------------------
// The script's name, drawn as a link that opens the file it came from in
// whatever this machine uses for .cpp files - the same thing double-clicking
// it in the Assets panel does. See ShellOpen.h.
//
// WHY A LOOKUP IS NEEDED AT ALL: a ScriptComponent stores a name, not a path,
// so the file has to be found again. That walks the assets tree, which is why
// it happens on the CLICK rather than every frame while the link is merely on
// screen - see ScriptPathFromName.
//
// The consequence is that the link is drawn without knowing whether there is
// a file behind it. Reporting a miss when somebody asks is honest and costs
// nothing; scanning the disk sixty times a second to grey out a label would
// cost a great deal.
void DrawScriptNameLink(const eng::ScriptComponent& script, ImVec4 colour) {
    ImGui::PushStyleColor(ImGuiCol_TextLink, colour);
    const bool clicked = ImGui::TextLink(script.ScriptName().c_str());
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Open %s in the program this machine uses for that kind "
                          "of file", script.ScriptName().c_str());
    }
    if (!clicked) {
        return;
    }

    const std::string path = ScriptPathFromName(script.ScriptName());
    if (path.empty()) {
        // AT ERROR, not Warning: the project's log threshold discards
        // warnings, and a link that does nothing and says nothing is the
        // exact failure this feature exists to avoid. Same reasoning as
        // ShellOpen.h.
        ENGINE_LOG_ERROR(eng::Channels::kEditor,
                         "no file for the script '{}' was found under assets/ - a "
                         "script is opened by looking for a file named after it",
                         script.ScriptName());
        return;
    }

    std::string error;
    if (OpenExternally(path, error)) {
        ENGINE_LOG_INFO(eng::Channels::kEditor, "opened {}", path);
    } else {
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not open '{}': {}", path, error);
    }
}

// ---------------------------------------------------------------------------
//  A script's variables.
//
//  The script says which ones it has - see engine/scene/ScriptFields.h for why
//  it has to say rather than be asked - and everything here is drawing them.
//
//  ==========================================================================
//  THE LABEL IS THE NAME FROM THE CODE, NOT A TIDIED-UP VERSION OF IT.
//
//  Unity turns `maxSpeed` into "Max Speed", which reads better and is the
//  wrong trade here. That name is also the KEY IN THE SCENE FILE, and this
//  project's scenes are meant to be opened and edited by hand - so a label
//  that does not match what is in the file would mean looking up the
//  translation every time. `maxSpeed` on screen and "maxSpeed" in the file.
//
//  ==========================================================================
//  WHAT THE OVERRIDE MARK IS FOR
//
//  A variable somebody changed is saved in the scene; one they left alone is
//  not, and takes whatever the code says. Those two look identical on screen -
//  both are just a number - so an overridden one gets a `revert` button beside
//  it. That is both the indicator and the way out.
// ---------------------------------------------------------------------------

// The longest text a string variable can be edited with here. Generous enough
// for any name or path in this project; the case that exceeds it is handled
// rather than truncated. See DrawStringField.
constexpr int kScriptStringMax = 256;

// How fast a drag box moves, taken from the number already in it.
//
// One fixed step cannot serve both a 0.2 blend and a 2000-pixel distance: at
// 0.1 per pixel the first is unusable and the second takes half a minute to
// cross. Scaling by the value gives fine control near zero and coarse control
// far out, which is what anybody would pick by hand for each field.
//
// The floor matters: without it a variable sitting at exactly 0 would have a
// step of 0 and the drag would be dead.
float DragStepFor(float value) {
    return std::max(0.01f, std::abs(value) * 0.01f);
}

// Text is the one type that cannot be handed to ImGui as a pointer, because
// ImGui edits a char buffer and the variable is a std::string.
bool DrawStringField(const eng::ScriptField& field) {
    std::string* text = field.AsString();
    if (text == nullptr) {
        return false;
    }

    // ======================================================================
    //  LONGER THAN THE BUFFER: SHOWN, NOT EDITED.
    //
    //  Copying it in would truncate it, and the first keystroke would then
    //  write the short version back over the variable - deleting the rest of
    //  somebody's text with nothing on screen having warned them. Refusing to
    //  edit it is the only safe option, and the file is still there.
    // ======================================================================
    if (text->size() >= static_cast<std::size_t>(kScriptStringMax)) {
        ImGui::BeginDisabled();
        char preview[80];
        std::snprintf(preview, sizeof(preview), "%.60s...", text->c_str());
        ImGui::InputText(field.name.c_str(), preview, sizeof(preview),
                         ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
        ImGui::TextDisabled("%zu characters - too long to edit here, so it is shown "
                            "read-only. Change it in the scene file.", text->size());
        return false;
    }

    char buffer[kScriptStringMax];
    std::snprintf(buffer, sizeof(buffer), "%s", text->c_str());
    if (ImGui::InputText(field.name.c_str(), buffer, sizeof(buffer))) {
        *text = buffer;
        return true;
    }
    return false;
}

// One variable, drawn with whichever widget suits its type. True on a frame
// the value was actually changed.
bool DrawFieldWidget(const eng::ScriptField& field) {
    const char* label = field.name.c_str();

    switch (field.type) {
        case eng::ScriptFieldType::Float: {
            float* value = field.AsFloat();
            if (value == nullptr) {
                break;
            }
            if (field.ranged) {
                return ImGui::SliderFloat(label, value, field.min, field.max);
            }
            return ImGui::DragFloat(label, value, DragStepFor(*value));
        }

        case eng::ScriptFieldType::Int: {
            int* value = field.AsInt();
            if (value == nullptr) {
                break;
            }
            if (field.ranged) {
                return ImGui::SliderInt(label, value, static_cast<int>(field.min),
                                        static_cast<int>(field.max));
            }
            // The same step the sprite's draw order uses, because a whole
            // number wants one pixel of mouse per unit and not less.
            return ImGui::DragInt(label, value, 0.2f);
        }

        case eng::ScriptFieldType::Bool: {
            bool* value = field.AsBool();
            if (value == nullptr) {
                break;
            }
            return ImGui::Checkbox(label, value);
        }

        case eng::ScriptFieldType::String:
            return DrawStringField(field);

        case eng::ScriptFieldType::Vec2: {
            eng::Vec2* value = field.AsVec2();
            if (value == nullptr) {
                break;
            }
            float xy[2] = {value->x, value->y};
            // ONE step for both halves, taken from the larger. Two different
            // sensitivities on one widget would make a diagonal drag curve.
            const float step =
                DragStepFor(std::max(std::abs(xy[0]), std::abs(xy[1])));
            if (ImGui::DragFloat2(label, xy, step)) {
                *value = eng::Vec2{xy[0], xy[1]};
                return true;
            }
            return false;
        }

        case eng::ScriptFieldType::Color: {
            eng::Color* value = field.AsColor();
            if (value == nullptr) {
                break;
            }
            // ImGui works in 0-1 floats and the engine stores 0-255 bytes,
            // the same conversion DrawSprite does for a tint.
            float rgba[4] = {value->r / 255.0f, value->g / 255.0f, value->b / 255.0f,
                             value->a / 255.0f};
            if (ImGui::ColorEdit4(label, rgba)) {
                *value = eng::Color{static_cast<unsigned char>(rgba[0] * 255.0f),
                                    static_cast<unsigned char>(rgba[1] * 255.0f),
                                    static_cast<unsigned char>(rgba[2] * 255.0f),
                                    static_cast<unsigned char>(rgba[3] * 255.0f)};
                return true;
            }
            return false;
        }
    }

    // Reached only when a field's type and its pointer disagree, which the
    // AsFloat/AsInt/... accessors refuse to act on rather than casting
    // regardless. Saying so beats drawing a blank row.
    ImGui::TextDisabled("%s - cannot be shown (%s)", label,
                        eng::ScriptFieldTypeName(field.type));
    return false;
}

// What hovering a variable explains: the script's own note, the type, and
// whether this entity has its own value or is using the code's.
void DrawFieldTooltip(const eng::ScriptComponent& script,
                      const eng::ScriptField& field, bool overridden) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        return;
    }
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);

    if (!field.tooltip.empty()) {
        ImGui::TextUnformatted(field.tooltip.c_str());
        ImGui::Separator();
    }
    ImGui::TextDisabled("%s", eng::ScriptFieldTypeName(field.type));

    if (overridden) {
        ImGui::TextDisabled("saved on this entity as %s",
                            script.SavedValues().at(field.name).dump().c_str());
    } else {
        ImGui::TextDisabled("using the value from the script's own code - change it "
                            "here and it is saved on this entity only");
    }

    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// True when it drew anything, so the caller can decide whether a separator
// belongs under it. Returned rather than asked for a second time: the answer
// costs a list of strings to work out, and working it out twice a frame for a
// question with one answer is the kind of waste that spreads.
bool DrawScriptFields(eng::ScriptComponent& script) {
    const eng::ScriptFields&       fields = script.Fields();
    const std::vector<std::string> unused = script.UnusedSavedValues();

    // A script that exposes nothing and has nothing saved gets no section at
    // all. Most scripts are in that position and an empty "Variables" header
    // on every one of them would be noise.
    if (fields.Empty() && unused.empty()) {
        return false;
    }

    ImGui::SeparatorText("Variables");

    for (const eng::ScriptField& field : fields.All()) {
        // Keyed on the NAME, so the widget keeps its identity when another
        // variable is added above it. An index would hand one field's
        // half-typed text to its neighbour.
        ImGui::PushID(field.name.c_str());

        const bool overridden = script.IsFieldOverridden(field.name);

        if (DrawFieldWidget(field)) {
            // The widget has already written the new value through the
            // field's own pointer; this records it as the saved one. Exactly
            // one piece of code - FieldToJson - knows how each type is
            // stored, and it is in the engine beside the code that reads it
            // back.
            script.SaveFieldValue(field.name);
            EditorState::Get().dirty = true;
        }
        DrawFieldTooltip(script, field, overridden);

        // DIRECTLY AFTER THE WIDGET, so it reads as belonging to it. Anything
        // that has to be said about the value goes underneath, below.
        if (overridden) {
            ImGui::SameLine();
            if (ImGui::SmallButton("revert")) {
                script.RevertFieldValue(field.name);
                EditorState::Get().dirty = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Forget this entity's value and go back to the one "
                                  "written in the script");
            }

            // A SAVED VALUE THAT WILL NOT LOAD. The engine reports this to
            // the Console when the scene opens, and then it is over - so it
            // is also shown here, next to the variable it is about, for as
            // long as it is true.
            if (!eng::JsonFitsFieldType(field.type,
                                        script.SavedValues().at(field.name))) {
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                                   "the scene file has %s here, which is not a %s - "
                                   "the script's own value is being used",
                                   script.SavedValues().at(field.name).dump().c_str(),
                                   eng::ScriptFieldTypeName(field.type));
            }
        }

        // A RANGED VALUE OUTSIDE ITS RANGE, which is otherwise invisible: a
        // slider draws it pinned at whichever end it overshot, so 500 on a
        // 1-to-200 range looks exactly like 200. It can get there from a
        // hand-edited scene file or from the script setting it, because
        // nothing clamps on load - deliberately, since silently rewriting a
        // number somebody typed is worse than showing it.
        //
        // TOUCHING THE SLIDER WILL pull it into range, and that is worth
        // warning about before it happens rather than after.
        if (field.ranged) {
            const float value = field.type == eng::ScriptFieldType::Int
                                    ? static_cast<float>(*field.AsInt())
                                    : *field.AsFloat();
            if (value < field.min || value > field.max) {
                ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                                   "%.3g is outside %.3g to %.3g - the slider shows it "
                                   "at the end, and moving it will bring it in",
                                   static_cast<double>(value),
                                   static_cast<double>(field.min),
                                   static_cast<double>(field.max));
            }
        }

        ImGui::PopID();
    }

    if (!fields.Empty()) {
        ImGui::TextDisabled("ctrl-click a number to type it exactly");
    }

    // ---- values nothing is using -----------------------------------------
    if (unused.empty()) {
        return true;
    }

    ImGui::Spacing();

    // THE TWO CAUSES ARE NOT THE SAME PROBLEM, so they do not get the same
    // message. An unresolved script claims nothing, so everything saved looks
    // unused and none of it is wrong - it is waiting. A running script with
    // leftovers has had a variable renamed or removed.
    if (script.IsResolved()) {
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                           "%zu saved value(s) this script has no variable for",
                           unused.size());
        ImGui::TextWrapped("Renamed or removed since they were saved. They are kept and "
                           "still written to the scene file, so nothing is lost if the "
                           "variable comes back under its old name.");
    } else {
        ImGui::TextDisabled("%zu saved value(s), kept until the script compiles",
                            unused.size());
    }

    if (ImGui::TreeNode("Show them")) {
        for (const std::string& key : unused) {
            ImGui::BulletText("%s = %s", key.c_str(),
                              script.SavedValues().at(key).dump().c_str());
        }
        ImGui::TreePop();
    }

    // NOT OFFERED WHILE THE SCRIPT IS UNRESOLVED, where "unused" means all of
    // them and this button would be a one-click way to delete every setting
    // the component was keeping safe. ForgetUnusedSavedValues refuses in that
    // state as well; this is the half somebody can see.
    if (script.IsResolved() && ImGui::Button("Forget them")) {
        const std::size_t gone = script.ForgetUnusedSavedValues();
        EditorState::Get().dirty = true;
        ENGINE_LOG_INFO(eng::Channels::kEditor,
                        "dropped {} saved value(s) that the script '{}' no longer has "
                        "variables for", gone, script.ScriptName());
    }
    return true;
}

void DrawScript(eng::ScriptComponent& script) {
    if (script.IsResolved()) {
        DrawScriptNameLink(script, ImVec4(0.55f, 0.85f, 0.60f, 1.0f));

        // WHICH HOOKS THIS SCRIPT ACTUALLY HAS.
        //
        // Hooks are found by name while the script compiles, so a misspelled
        // OnUpdate is not an error - it is a function nobody calls. Showing
        // the list here turns "why is nothing happening?" into something you
        // can answer by looking at the entity you are already looking at.
        const std::string hooks = eng::DescribeHooks(script.Hooks());
        ImGui::TextDisabled("compiled in and running");
        ImGui::TextWrapped("Hooks: %s", hooks.c_str());
    } else {
        // RED AND LOUD. A script that silently does nothing because it was
        // never compiled is the worst failure this feature can have, so it is
        // the most prominent thing in the panel.
        //
        // STILL A LINK, and if anything more useful as one: "this is not
        // running" is the moment you most want to open the file and find out
        // why. Not being compiled in does not mean the file is missing.
        DrawScriptNameLink(script, ImVec4(0.95f, 0.45f, 0.40f, 1.0f));
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), " -  NOT FOUND");
        ImGui::TextWrapped("No script with that name is compiled into this build, so it "
                           "is attached and will be saved, but it does not run. Rebuild "
                           "the project and start the editor again - the connection is "
                           "by name, so nothing needs reattaching.");
    }

    // THE VARIABLES, above the attach controls below. Tuning a script is what
    // somebody does repeatedly; choosing which script it is happens once.
    if (DrawScriptFields(script)) {
        ImGui::SeparatorText("Script");
    }

    // A list of what IS available, so a typo is one click from fixed rather
    // than a hunt through the scripts folder.
    if (ImGui::BeginCombo("Bind to", script.ScriptName().c_str())) {
        if (eng::ScriptRegistry::Count() == 0) {
            ImGui::TextDisabled("no scripts are compiled into this build");
        }
        eng::ScriptRegistry::ForEachScript([&](const char* name) {
            if (ImGui::Selectable(name, script.ScriptName() == name)) {
                script.SetScriptName(name);
                EditorState::Get().dirty = true;
            }
        });
        ImGui::EndCombo();
    }

    ImGui::TextDisabled("or drag a .cpp from the Assets panel onto this window");
}

} // namespace

void InspectorPanel::ApplyPendingRemoval() {
    if (!m_pendingRemoval.requested) {
        return;
    }

    // Taken and CLEARED FIRST, so an early return below cannot leave a
    // request that tries again on every frame for the rest of the session.
    const PendingRemoval request = m_pendingRemoval;
    m_pendingRemoval             = {};

    eng::Entity* entity = eng::Engine::Get().GetScene().Get(request.entity);
    if (entity == nullptr) {
        return;   // destroyed between the click and here; the id did its job
    }

    // CHECKED AGAIN, not trusted. The index was true when the x was clicked,
    // and anything that ran in between could have changed the list - so the
    // component at that position is confirmed to still be the kind that may
    // be removed before anything is erased.
    eng::Component* component = entity->ComponentAt(request.index);
    if (component == nullptr) {
        return;
    }
    const std::string typeName = component->TypeName();
    if (typeName == eng::TransformComponent::kTypeName) {
        ENGINE_LOG_ERROR(eng::Channels::kEditor,
                         "refused to remove the transform from '{}' - every entity "
                         "has one", entity->Name());
        return;
    }

    if (entity->RemoveComponentAt(request.index)) {
        EditorState::Get().dirty = true;
        ENGINE_LOG_INFO(eng::Channels::kEditor, "removed {} from '{}'", typeName,
                        entity->Name());
    }
}

void InspectorPanel::Draw() {
    // FIRST, before anything reads the component list - including the early
    // return below for "nothing is selected". Carrying it out at the end of
    // Draw instead would look the same almost always, and then quietly strand
    // the request on the one path that returns early: click the x, click away
    // before the next frame, and the removal would sit there unapplied.
    //
    // The cost is that the component stays on screen for the remainder of the
    // frame its x was clicked on, which at sixty frames a second nobody sees.
    ApplyPendingRemoval();

    EditorState& state = EditorState::Get();
    eng::Scene&  scene = eng::Engine::Get().GetScene();

    // Looked up from the ID every frame. A selection that has been destroyed
    // comes back as nothing and is reported, rather than crashing - which is
    // the entire reason the selection is an id and not a pointer.
    eng::Entity* entity = scene.Get(state.selected);
    if (entity == nullptr) {
        if (!state.selected.IsNull()) {
            ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                               "the selected entity no longer exists");
            if (ImGui::Button("Clear selection")) {
                state.ClearSelection();
            }
        } else {
            ImGui::TextDisabled("select an entity in the Hierarchy");
        }
        return;
    }

    // ---- the WHOLE PANEL is a drop target --------------------------------
    //
    // Not a strip or a header - the entire window, so an image or a script can
    // be dropped anywhere on it. Done before the fields are drawn so that a
    // widget directly under the cursor still wins.
    if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->Rect(),
                                         ImGui::GetID("##inspector_drop"))) {
        std::string message;
        for (const char* payloadId : {kPayloadTexture, kPayloadScript}) {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadId);
            if (payload == nullptr) {
                continue;
            }
            const auto* path = static_cast<const char*>(payload->Data);
            if (ApplyAssetToEntity(state.selected, path, message)) {
                ENGINE_LOG_INFO(eng::Channels::kEditor, "{}", message);
            } else {
                ENGINE_LOG_WARN(eng::Channels::kEditor, "{}", message);
            }
        }
        ImGui::EndDragDropTarget();
    }

    const bool running = !eng::Engine::Get().Clock().IsPaused();

    // ---- when several things are selected ---------------------------------
    //
    // The components below belong to the ACTIVE entity only, and saying so
    // matters: editing a field here with eight entities selected changes one
    // of them, and somebody who thought otherwise would not find out until
    // much later. The alternative - editing every selected entity at once -
    // is a real feature with real questions behind it (what happens when
    // they disagree?) and is not something to do by accident.
    if (state.selection.size() > 1) {
        ImGui::TextColored(ImVec4(0.55f, 0.78f, 0.95f, 1.0f), "%zu entities selected",
                           state.selection.size());

        if (ImGui::Button("Destroy all")) {
            for (const eng::EntityId id : state.selection) {
                eng::DeferredOps::QueueDestroy(id);
            }
            state.ClearSelection();
            state.dirty = true;
        }

        if (ImGui::TreeNode("Show the list")) {
            for (const eng::EntityId id : state.selection) {
                if (const eng::Entity* member = scene.Get(id)) {
                    // Clicking one makes it the active entity, so the list
                    // doubles as a way of stepping through the selection
                    // without losing it.
                    if (ImGui::Selectable(member->Name().c_str(), id == state.selected)) {
                        state.selected = id;
                    }
                }
            }
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::TextDisabled("below: the components of the active entity only");
    }

    ImGui::Text("%s", entity->Name().c_str());

    ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
    if (ImGui::Button("Destroy")) {
        // Answer 3: through the deferred queue, like everything else.
        eng::DeferredOps::QueueDestroy(entity->Id());
    }

    ImGui::Separator();

    // WALKED BY INDEX rather than with ForEachComponent, because removing
    // needs to say WHICH component - not which type. An entity can hold
    // several scripts, and "remove the ScriptComponent" would always take the
    // first one however far down the list the x was clicked. See
    // Entity::RemoveComponentAt.
    //
    // Re-reading ComponentCount each time round is what makes it safe for the
    // body to add a component, which is the same reason ForEachComponent is
    // written this way. Removal is deferred and so does not come into it.
    for (std::size_t i = 0; i < entity->ComponentCount(); ++i) {
        eng::Component* componentPtr = entity->ComponentAt(i);
        if (componentPtr == nullptr) {
            continue;
        }
        eng::Component& component = *componentPtr;

        // PushID makes every widget inside this block unique to this
        // component, so two components with a field called "Offset" do not end
        // up sharing one widget.
        //
        // Keyed on the ADDRESS rather than the index, so that a header you
        // opened stays open when something above it is removed and everything
        // below shifts up a place.
        ImGui::PushID(&component);

        const std::string_view typeName = component.TypeName();

        // THE TRANSFORM IS THE ONE THAT STAYS. Every entity has one by
        // definition; see the note in InspectorPanel.h.
        const bool removable = typeName != eng::TransformComponent::kTypeName;

        // A SCRIPT IS NAMED AFTER THE SCRIPT, not after the class that holds
        // it. "ScriptComponent" is an implementation detail of the engine;
        // what you attached was PlayerMovement, and that is what you are
        // looking for when you scan the panel.
        //
        // It also makes several scripts on one entity tellable apart. Three
        // headers all reading "ScriptComponent" mean opening each one to find
        // out which is which - and now that a header carries a remove button,
        // guessing is expensive.
        //
        // The "(Script)" suffix is how Unity writes the same thing, and it
        // earns its place: without it a bare "PlayerMovement" sitting between
        // "SpriteComponent" and "AABBColliderComponent" gives no clue what
        // kind of thing it is.
        //
        // EVERYTHING AFTER ### IS THE IDENTITY, and the part before it is
        // only what you read. ImGui normally derives a widget's identity from
        // its label, which would mean rebinding a script - renaming this
        // header - counted as a different header, and the one you had open
        // would spring back to its default. The visible text is now free to
        // change without the panel rearranging itself underneath you. See the
        // note about widget identity in Panel.h.
        char header[128];
        if (typeName == eng::ScriptComponent::kTypeName) {
            const std::string& scriptName =
                static_cast<eng::ScriptComponent&>(component).ScriptName();
            if (scriptName.empty()) {
                std::snprintf(header, sizeof(header),
                              "Script (nothing bound yet)###component");
            } else {
                std::snprintf(header, sizeof(header), "%s (Script)###component",
                              scriptName.c_str());
            }
        } else {
            std::snprintf(header, sizeof(header), "%s###component",
                          component.TypeName());
        }

        // CollapsingHeader draws its own x when given somewhere to report it,
        // which is the ordinary way a panel like this offers "remove". `keep`
        // comes back false on the frame the x is clicked.
        bool       keep = true;
        const bool open = removable
                              ? ImGui::CollapsingHeader(header, &keep,
                                                        ImGuiTreeNodeFlags_DefaultOpen)
                              : ImGui::CollapsingHeader(header,
                                                        ImGuiTreeNodeFlags_DefaultOpen);

        // The same thing by right-click, because an x is easy to miss and
        // impossible to find by looking for the word "remove". Attached to the
        // header, which is the item just drawn.
        if (ImGui::BeginPopupContextItem("component-actions")) {
            if (ImGui::MenuItem("Remove Component", nullptr, false, removable)) {
                keep = false;
            }
            if (!removable) {
                ImGui::TextDisabled("Every entity has a transform.");
            }
            ImGui::EndPopup();
        }

        if (!removable && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Every entity has a transform, so this one cannot "
                              "be removed.");
        }

        if (!keep) {
            // RECORDED, NOT DONE. This loop is walking the component list;
            // erasing from it here shifts every later component down a place
            // and the loop would step straight over one. Carried out by
            // ApplyPendingRemoval at the end of Draw.
            m_pendingRemoval.entity    = entity->Id();
            m_pendingRemoval.index     = i;
            m_pendingRemoval.requested = true;
        }

        if (open) {
            // Work out which kind of component this is and call the matching
            // draw function. Comparing type NAMES is the same way the scene
            // file identifies components.
            const std::string_view type = typeName;

            if (type == eng::TransformComponent::kTypeName) {
                DrawTransform(static_cast<eng::TransformComponent&>(component), running);
            } else if (type == eng::SpriteComponent::kTypeName) {
                DrawSprite(static_cast<eng::SpriteComponent&>(component));
            } else if (type == eng::BoxColliderComponent::kTypeName ||
                       type == eng::CircleColliderComponent::kTypeName ||
                       type == eng::CapsuleColliderComponent::kTypeName ||
                       type == eng::PolygonColliderComponent::kTypeName ||
                       type == eng::EdgeColliderComponent::kTypeName ||
                       type == eng::TilemapColliderComponent::kTypeName) {
                DrawCollider(static_cast<eng::ColliderComponent&>(component), scene);
            } else if (type == eng::Rigidbody2DComponent::kTypeName) {
                DrawRigidbody(static_cast<eng::Rigidbody2DComponent&>(component));
            } else if (type == eng::TilemapComponent::kTypeName) {
                DrawTilemap(static_cast<eng::TilemapComponent&>(component));
            } else if (type == eng::SpinComponent::kTypeName) {
                DrawSpin(static_cast<eng::SpinComponent&>(component));
            } else if (type == eng::ScriptComponent::kTypeName) {
                DrawScript(static_cast<eng::ScriptComponent&>(component));
            } else if (type == eng::CameraComponent::kTypeName) {
                DrawCamera(static_cast<eng::CameraComponent&>(component));
            } else if (type == eng::UIRectComponent::kTypeName) {
                DrawUIRect(static_cast<eng::UIRectComponent&>(component));
            } else if (type == eng::UITextComponent::kTypeName) {
                DrawUIText(static_cast<eng::UITextComponent&>(component));
            } else if (type == eng::UIImageComponent::kTypeName) {
                DrawUIImage(static_cast<eng::UIImageComponent&>(component));
            } else if (type == eng::UIButtonComponent::kTypeName) {
                DrawUIButton(static_cast<eng::UIButtonComponent&>(component));
            } else if (type == eng::UISliderComponent::kTypeName) {
                DrawUISlider(static_cast<eng::UISliderComponent&>(component));
            } else {
                // Not an error. Somebody may write their own component type,
                // and the Inspector should say so rather than pretend it is
                // not there.
                ImGui::TextDisabled("no editor written for this component type yet");
            }
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::SeparatorText("Add component");

    // The list of buttons is generated from the factory, so a component type
    // registered anywhere appears here with no edit to this file.
    eng::ComponentFactory::ForEachType([&](const char* typeName) {
        if (entity->FindComponent(typeName) != nullptr) {
            return;   // it already has one
        }
        char label[96];
        std::snprintf(label, sizeof(label), "+ %s", typeName);
        if (ImGui::SmallButton(label)) {
            entity->AddComponent(typeName);
            EditorState::Get().dirty = true;
        }
    });
}

} // namespace editor
