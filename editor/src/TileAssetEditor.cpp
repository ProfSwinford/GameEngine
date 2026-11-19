#include "TileAssetEditor.h"

#include "AssetDragDrop.h"   // kPayloadTexture, for dropping art on a tile
#include "EditorApp.h"
#include "TilePaint.h"

#include <engine/core/WorldUnit.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace editor {

// ---------------------------------------------------------------------------
//  Editing the chosen tile
// ---------------------------------------------------------------------------

namespace {

// Draws one ASSEMBLED tile - all four quarters, picked for `mask` - at a given
// place on screen.
//
// Through the draw list rather than four ImGui::Image calls, because the
// quarters have to ABUT EXACTLY. Laid out with SameLine the style's item
// spacing would open a gap down the middle of every tile, and fighting the
// layout for four rectangles whose positions are already known is more code
// than placing them directly.
void DrawAssembledTile(ImDrawList* draw, const eng::TileAsset& asset,
                       unsigned char mask, ImVec2 topLeft, float size, int frame) {
    const eng::TextureRef& sheet = asset.ArtTexture();
    if (!asset.HasArt() || !sheet || sheet->native == nullptr ||
        sheet->width <= 0 || sheet->height <= 0) {
        return;
    }

    // AN IMAGE TILE HAS NOTHING TO ASSEMBLE, so every arrangement of
    // neighbours is the same square. Drawn here rather than refused, because
    // this is what the brush cursor and the palette grid both call.
    if (!asset.IsAutoTile()) {
        const float wide = static_cast<float>(asset.TileSize()) /
                           static_cast<float>(sheet->width);
        const float u0   = static_cast<float>(frame) * wide;
        draw->AddImage(reinterpret_cast<ImTextureID>(sheet->native), topLeft,
                       ImVec2(topLeft.x + size, topLeft.y + size), ImVec2(u0, 0.0f),
                       ImVec2(u0 + wide, 1.0f));
        return;
    }

    const eng::QuadrantSlots slots = eng::ResolveQuadrants(mask);

    const float half        = size * 0.5f;
    const float sheetWidth  = static_cast<float>(sheet->width);
    const float sheetHeight = static_cast<float>(sheet->height);

    for (int q = 0; q < eng::kQuadrantCount; ++q) {
        const auto             quadrant = static_cast<eng::Quadrant>(q);
        const eng::SpriteRect  source =
            eng::QuadrantSourceRect(slots[quadrant], quadrant, asset.TileSize(), frame);

        const bool right = (quadrant == eng::Quadrant::TopRight ||
                            quadrant == eng::Quadrant::BottomRight);
        const bool bottom = (quadrant == eng::Quadrant::BottomLeft ||
                             quadrant == eng::Quadrant::BottomRight);

        const ImVec2 corner(topLeft.x + (right ? half : 0.0f),
                            topLeft.y + (bottom ? half : 0.0f));
        const ImVec2 far(corner.x + half, corner.y + half);

        const ImVec2 uv0(static_cast<float>(source.x) / sheetWidth,
                         static_cast<float>(source.y) / sheetHeight);
        const ImVec2 uv1(static_cast<float>(source.x + source.w) / sheetWidth,
                         static_cast<float>(source.y + source.h) / sheetHeight);

        draw->AddImage(reinterpret_cast<ImTextureID>(sheet->native), corner, far,
                       uv0, uv1);
    }
}

// Small readers, so a missing key is a default rather than an exception.
// ---------------------------------------------------------------------------
//  Which component types a tile may carry.
//
//  ALMOST ALL OF THEM, which is the point: a tile is edited to the level a
//  prefab is, and a list of blessed types would go stale the first time
//  somebody wrote a component of their own. These three are refused because
//  they cannot mean anything here rather than because they were not thought
//  of:
//
//    TransformComponent        every entity has one, and the CELL decides
//                              where a tile object sits. One here would be
//                              ignored at best and would drag the object off
//                              its own cell at worst.
//    TilemapComponent          a map inside a tile of a map.
//    TilemapColliderComponent  likewise, and the tile already says whether it
//                              is solid with "collision".
// ---------------------------------------------------------------------------
bool TileMayCarry(std::string_view typeName) {
    return typeName != eng::TransformComponent::kTypeName &&
           typeName != eng::TilemapComponent::kTypeName &&
           typeName != eng::TilemapColliderComponent::kTypeName;
}

// A collider, or nullptr. BY TYPE NAME rather than by dynamic_cast, so that
// the preview below stays working for a collider written later: the names are
// what the file uses, and a new one will be in this list the day it is added
// to the Inspector's own dispatch.
eng::ColliderComponent* AsCollider(eng::Component* component) {
    if (component == nullptr) {
        return nullptr;
    }
    const std::string_view type = component->TypeName();
    if (type == eng::BoxColliderComponent::kTypeName ||
        type == eng::CircleColliderComponent::kTypeName ||
        type == eng::CapsuleColliderComponent::kTypeName ||
        type == eng::PolygonColliderComponent::kTypeName ||
        type == eng::EdgeColliderComponent::kTypeName) {
        return static_cast<eng::ColliderComponent*>(component);
    }
    return nullptr;
}

std::string JsonString(const eng::Json& node, const char* key, const char* fallback) {
    if (node.contains(key) && node[key].is_string()) {
        return node[key].get<std::string>();
    }
    return fallback;
}

bool JsonBool(const eng::Json& node, const char* key, bool fallback) {
    if (node.contains(key) && node[key].is_boolean()) {
        return node[key].get<bool>();
    }
    return fallback;
}

float JsonFloat(const eng::Json& node, const char* key, float fallback) {
    if (node.contains(key) && node[key].is_number()) {
        return node[key].get<float>();
    }
    return fallback;
}

} // namespace

const std::vector<std::string>& TileAssetEditor::KnownTiles() {
    if (!m_knownScanned) {
        CollectTiles("", m_knownTiles);
        std::sort(m_knownTiles.begin(), m_knownTiles.end());
        m_knownScanned = true;
    }
    return m_knownTiles;
}

bool TileAssetEditor::LoadEditTarget(const std::string& virtualPath) {
    m_editPath     = virtualPath;
    m_editJson     = eng::Json::object();
    m_editLoaded   = false;
    m_editDirty    = false;
    m_previewFrame = 0;
    m_editMessage.clear();

    std::string text;
    if (!eng::FileSystem::ReadTextFile(virtualPath, text, m_editMessage)) {
        return false;
    }

    m_editJson = eng::ParseJson(text, m_editMessage);
    if (!m_editMessage.empty()) {
        return false;
    }
    if (!m_editJson.is_object()) {
        m_editMessage = "a .otile must be a JSON object";
        return false;
    }

    m_editLoaded = true;
    RebuildLiveComponents();
    return true;
}

void TileAssetEditor::RebuildLiveComponents() {
    m_live.clear();
    m_liveSnapshot.clear();

    const auto list = m_editJson.find("components");
    if (list == m_editJson.end() || !list->is_array()) {
        return;
    }

    for (const eng::Json& entry : *list) {
        std::unique_ptr<eng::Component> made;

        const std::string type = JsonString(entry, "type", "");
        if (!type.empty()) {
            made = eng::ComponentFactory::Create(type);
        }

        eng::Json snapshot = eng::Json::object();
        if (made) {
            // BEST EFFORT. A field this build does not understand, or one
            // that will not read, is reported against the .otile when the
            // engine loads it - see TileAsset::DeserializeComponents. Here it
            // simply means the widget shows the default, and the entry is
            // left exactly as written until somebody touches one.
            std::string why;
            made->Deserialize(entry, why);

            if (made->Serialize(snapshot)) {
                snapshot["type"] = made->TypeName();
            }
        }

        m_live.push_back(std::move(made));
        m_liveSnapshot.push_back(std::move(snapshot));
    }
}

// ===========================================================================
//  The tile's components
// ===========================================================================

void TileAssetEditor::DrawComponents(const ComponentBodyDrawer& drawComponentBody) {
    ImGui::SeparatorText("Components");
    ImGui::TextWrapped(
        "What a cell painted with this tile becomes. Every cell gets the same "
        "components, because a tile is a KIND of thing - two spikes that do "
        "different damage are two .otile files, the way two enemies that do "
        "are two prefabs.");
    ImGui::TextDisabled("A tile with none costs nothing; one with any costs an "
                        "entity per painted cell, so keep them for the sparse "
                        "interesting things rather than for the ground.");

    eng::Json*        list  = nullptr;
    const auto        found = m_editJson.find("components");
    if (found != m_editJson.end() && found->is_array()) {
        list = &(*found);
    }

    const std::size_t count = (list == nullptr) ? 0u : list->size();

    // The buffer can change under us - Revert, or a different tile selected -
    // so the live list is rebuilt whenever the two disagree about how many
    // there are.
    if (m_live.size() != count) {
        RebuildLiveComponents();
    }

    int removeAt = -1;

    for (std::size_t i = 0; i < count; ++i) {
        eng::Json&        entry = (*list)[i];
        const std::string type  = JsonString(entry, "type", "(no type)");

        // KEYED ON THE LIVE COMPONENT'S ADDRESS, as the entity Inspector is,
        // so a header you opened stays open when something ABOVE it is
        // removed and everything below shifts up a place. An entry with no
        // live component has no address to key on and falls back to its
        // index, which is the best there is for one that cannot be edited
        // anyway.
        eng::Component* live = (i < m_live.size()) ? m_live[i].get() : nullptr;
        if (live != nullptr) {
            ImGui::PushID(live);
        } else {
            ImGui::PushID(static_cast<int>(i));
        }

        // EVERYTHING AFTER ### IS THE IDENTITY, as in the entity Inspector,
        // so a header you opened stays open when the text in front of it
        // changes. See the note about widget identity in Panel.h.
        char header[160];
        std::snprintf(header, sizeof(header), "%s###tile-component", type.c_str());

        bool       keep = true;
        const bool open =
            ImGui::CollapsingHeader(header, &keep, ImGuiTreeNodeFlags_DefaultOpen);
        if (!keep) {
            removeAt = static_cast<int>(i);
        }

        if (open) {
            if (live == nullptr) {
                ImGui::TextWrapped(
                    "nothing in this build defines '%s', so its fields cannot be "
                    "shown. It is kept exactly as written and saving does not "
                    "disturb it - build the code that defines it and it will "
                    "appear here.",
                    type.c_str());
            } else {
                // ==========================================================
                //  THE SCENE IS NOT WHAT IS BEING EDITED HERE.
                //
                //  Every one of the Inspector's component editors calls
                //  EditorState::MarkDirty when a widget moves, and on an
                //  entity that is exactly right. On a tile it would say the
                //  SCENE has unsaved changes because somebody nudged a
                //  collider in a file - and then Ctrl+S would write a scene
                //  nobody had touched.
                //
                //  So the scene's flag is put back the way it was found. The
                //  tile's own "unsaved" is m_editDirty, set by the write-back
                //  below, which is the honest answer to a different question.
                // ==========================================================
                const bool sceneWasDirty = EditorState::Get().IsDirty();

                drawComponentBody(*live);

                if (!sceneWasDirty) {
                    EditorState::Get().ClearDirty();
                }

                // ==========================================================
                //  WRITTEN BACK, AND ONLY WHEN SOMETHING MOVED.
                //
                //  Compared against the last snapshot rather than against
                //  the file: a hand-written entry naming two keys differs
                //  from a full Serialize on the very first frame, so
                //  comparing with the file would mark a tile unsaved and
                //  expand every entry in it merely for being looked at.
                // ==========================================================
                eng::Json rebuilt = eng::Json::object();
                if (live->Serialize(rebuilt)) {
                    rebuilt["type"] = live->TypeName();

                    if (i >= m_liveSnapshot.size() || rebuilt != m_liveSnapshot[i]) {
                        entry       = rebuilt;
                        m_editDirty = true;
                        if (i < m_liveSnapshot.size()) {
                            m_liveSnapshot[i] = std::move(rebuilt);
                        }
                    }
                }
            }
        }

        ImGui::PopID();
    }

    if (count == 0) {
        ImGui::TextDisabled("none - this tile is scenery and collision, and "
                            "nothing else.");
    }

    // ---- taking one away ---------------------------------------------------
    if (removeAt >= 0 && list != nullptr) {
        list->erase(list->begin() + removeAt);

        // THE KEY GOES WHEN THE LAST ONE DOES, so that a tile which never had
        // components is byte-identical to one whose last component was
        // removed. An empty "components": [] left behind would be noise in
        // every file somebody had ever experimented in.
        if (list->empty()) {
            m_editJson.erase("components");
        }
        m_editDirty = true;
        RebuildLiveComponents();
    }

    // ---- adding one --------------------------------------------------------
    ImGui::Spacing();
    if (ImGui::Button("Add component...")) {
        ImGui::OpenPopup("add-tile-component");
    }

    if (ImGui::BeginPopup("add-tile-component")) {
        // GENERATED FROM THE FACTORY, so a component type registered anywhere
        // - including by a game's own code - appears here with no edit to
        // this file.
        eng::ComponentFactory::ForEachType([&](const char* typeName) {
            if (!TileMayCarry(typeName)) {
                return;
            }

            // ONE IS USUALLY ALL THERE IS, AND SOMETIMES IT IS NOT: a compound
            // body is several colliders, and a tile is as entitled to one as
            // an entity. The component itself answers - see
            // Component::AllowsSeveral.
            for (std::size_t i = 0; i < m_live.size(); ++i) {
                if (m_live[i] != nullptr &&
                    std::string_view(m_live[i]->TypeName()) == typeName &&
                    !m_live[i]->AllowsSeveral()) {
                    return;
                }
            }

            if (!ImGui::MenuItem(typeName)) {
                return;
            }

            // WRITTEN OUT THROUGH THE COMPONENT ITSELF, so the entry starts as
            // a complete and valid description rather than as a bare "type"
            // that every reader has to guess the rest of.
            std::unique_ptr<eng::Component> made =
                eng::ComponentFactory::Create(typeName);
            if (!made) {
                return;
            }

            eng::Json entry = eng::Json::object();
            if (!made->Serialize(entry)) {
                entry = eng::Json::object();
            }
            entry["type"] = typeName;

            if (!m_editJson.contains("components") ||
                !m_editJson["components"].is_array()) {
                m_editJson["components"] = eng::Json::array();
            }
            m_editJson["components"].push_back(std::move(entry));

            m_editDirty = true;
            RebuildLiveComponents();
        });
        ImGui::EndPopup();
    }
}

// ===========================================================================
//  One cell, as it will be
// ===========================================================================

void TileAssetEditor::DrawCellPreview(const eng::TileAsset& asset) {
    ImGui::SeparatorText("In a cell");

    constexpr float kSize = 160.0f;

    ImDrawList*  draw    = ImGui::GetWindowDrawList();
    const ImVec2 topLeft = ImGui::GetCursorScreenPos();
    const ImVec2 bottomRight(topLeft.x + kSize, topLeft.y + kSize);

    // A dark backing, so that a tile with no art - and a half-transparent
    // tint over nothing - is still something to look at.
    draw->AddRectFilled(topLeft, bottomRight, IM_COL32(26, 26, 30, 255));

    if (asset.HasArt()) {
        // THE FULLY-SURROUNDED SHAPE (every neighbour present), because that
        // is the one that fills its cell: an end piece or a corner leaves
        // part of the square empty and would make the collider look offset
        // when it is not.
        DrawAssembledTile(draw, asset, 0xFF, topLeft, kSize, m_previewFrame);
    } else {
        const eng::Color tint = asset.EditorTint();
        draw->AddRectFilled(topLeft, bottomRight,
                            IM_COL32(tint.r, tint.g, tint.b, 150));
    }

    // The cell's own edge.
    draw->AddRect(topLeft, bottomRight, IM_COL32(130, 130, 140, 220));

    // =======================================================================
    //  THE SQUARE IS ONE CELL, AND A CELL IS NOT THE ART'S PIXEL SIZE.
    //
    //  This used to divide by TileAsset::TileSize(), which is how many pixels
    //  across the PICTURE is - and a 16-pixel autotile sheet is routinely
    //  painted on a 32-pixel map, stretched to fill the cell, exactly as it
    //  is stretched to fill this square a few lines above. Meanwhile a
    //  collider's LocalSize() is a fraction of a cell, not of the art. So a
    //  16-pixel tile drew its collider at twice the size it really is, and
    //  the two were only ever in agreement because most sheets here happen to
    //  be 32.
    //
    //  ONE WORLD UNIT IS THE CELL THIS ASSUMES, and the legend says so,
    //  because a .otile does not know which map it will be painted on - a
    //  TilemapComponent's cellSize belongs to the map. The SIZE drawn is
    //  right whatever that turns out to be, since it is a fraction of the
    //  cell either way; only an OFFSET, which is in pixels, is read against
    //  this assumption.
    // =======================================================================
    const float cellPixels = eng::WorldUnit::Pixels();
    const float scale      = kSize / cellPixels;
    const ImVec2 centre(topLeft.x + kSize * 0.5f, topLeft.y + kSize * 0.5f);

    // ---- what "collision": "Grid" does, which is the whole cell ------------
    if (asset.IsSolid()) {
        draw->AddRectFilled(topLeft, bottomRight, IM_COL32(90, 170, 255, 45));
        draw->AddRect(topLeft, bottomRight, IM_COL32(90, 170, 255, 230), 0.0f, 0,
                      2.0f);
    }

    // ---- and where each collider component will actually sit ---------------
    //
    // IN THE CELL'S OWN SPACE. A tile object is created at the CENTRE of its
    // cell, so a collider's offset is measured from there - which is exactly
    // what is drawn, with y flipped because the world is y-up and the screen
    // is not.
    int colliders = 0;
    for (const std::unique_ptr<eng::Component>& component : m_live) {
        eng::ColliderComponent* collider = AsCollider(component.get());
        if (collider == nullptr) {
            continue;
        }
        ++colliders;

        const eng::Vec2 offset = collider->Offset();
        const eng::Vec2 size   = collider->LocalSize();

        const ImVec2 at(centre.x + offset.x * scale, centre.y - offset.y * scale);
        const ImVec2 half(size.x * 0.5f * scale, size.y * 0.5f * scale);

        const ImU32 fill = collider->IsTrigger() ? IM_COL32(255, 190, 70, 50)
                                                 : IM_COL32(120, 255, 140, 50);
        const ImU32 line = collider->IsTrigger() ? IM_COL32(255, 190, 70, 235)
                                                 : IM_COL32(120, 255, 140, 235);

        if (std::string_view(collider->TypeName()) ==
            eng::CircleColliderComponent::kTypeName) {
            const float radius = std::max(half.x, half.y);
            draw->AddCircleFilled(at, radius, fill);
            draw->AddCircle(at, radius, line, 0, 2.0f);
        } else {
            const ImVec2 lo(at.x - half.x, at.y - half.y);
            const ImVec2 hi(at.x + half.x, at.y + half.y);
            draw->AddRectFilled(lo, hi, fill);
            draw->AddRect(lo, hi, line, 0.0f, 0, 2.0f);
        }
    }

    ImGui::Dummy(ImVec2(kSize, kSize));

    // ---- the legend --------------------------------------------------------
    // NAMES BOTH NUMBERS, because they are different numbers and conflating
    // them is what made this preview wrong. The cell is what a collider is a
    // fraction of; the art is how many pixels the picture has.
    ImGui::TextDisabled("one cell at %.0fpx (one world unit)",
                        static_cast<double>(cellPixels));
    if (asset.HasArt() && asset.TileSize() > 0) {
        const float art = static_cast<float>(asset.TileSize());
        ImGui::SameLine();
        if (art == cellPixels) {
            ImGui::TextDisabled("- the art is %dpx, so one tile is one cell",
                                asset.TileSize());
        } else {
            ImGui::TextDisabled("- the art is %dpx, stretched %.2gx to fill it",
                                asset.TileSize(),
                                static_cast<double>(cellPixels / art));
        }
    }
    ImGui::TextDisabled("a map with its own cellSize scales all of this "
                        "together - the collider stays the same fraction of "
                        "the cell");

    if (asset.IsSolid()) {
        ImGui::TextColored(ImVec4(0.35f, 0.67f, 1.0f, 1.0f),
                           "blue: solid, the whole cell");
        ImGui::SameLine();
        ImGui::TextDisabled("(merged with its neighbours - see the map's statistics)");
    }
    if (colliders > 0) {
        ImGui::TextColored(ImVec4(0.47f, 1.0f, 0.55f, 1.0f),
                           "green: a collider component");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.27f, 1.0f), "amber: a trigger");
        ImGui::TextDisabled("one per painted cell, at the centre of its cell - "
                            "NOT merged.");
    }
    if (!asset.IsSolid() && colliders == 0) {
        ImGui::TextDisabled("nothing collides with this tile: \"collision\" is None "
                            "and it carries no collider.");
    }
}

bool TileAssetEditor::SaveEditTarget(std::string& outError) {
    const std::string text = m_editJson.dump(2) + "\n";
    if (!eng::FileSystem::WriteTextFile(m_editPath, text, outError)) {
        return false;
    }

    // Reload re-reads IN PLACE so every map pointing at this asset sees the
    // change. A tile that never loaded in the first place is not in the cache
    // for Reload to find, so it is simply loaded now - which is the case that
    // matters most, because the usual reason to edit a tile is that it is
    // broken.
    const bool cached = (eng::TileLibrary::Find(m_editPath) != nullptr);
    const bool reread = cached ? eng::TileLibrary::Reload(m_editPath, outError)
                               : (eng::TileLibrary::Load(m_editPath) != nullptr);
    if (!reread) {
        if (outError.empty()) {
            outError = "saved, but the file still will not load - see the Console";
        }
        return false;
    }

    RefreshTilemapsUsing(m_editPath);
    m_editDirty = false;
    return true;
}

void TileAssetEditor::RefreshTilemapsUsing(const std::string& otilePath) {
    eng::Scene* scene = eng::Scene::Active();
    if (scene == nullptr) {
        return;
    }

    scene->ForEach([&](eng::Entity& entity) {
        auto* map = entity.Find<eng::TilemapComponent>();
        if (map == nullptr) {
            return;
        }
        const std::vector<std::string>& palette = map->Palette();
        if (std::find(palette.begin(), palette.end(), otilePath) == palette.end()) {
            return;
        }

        map->RefreshPalette();

        // The collider watches the map's REVISION, which has not moved: no
        // cell changed. Only the meaning of a cell did.
        if (auto* collider = entity.Find<eng::TilemapColliderComponent>()) {
            collider->Invalidate();
        }
    });
}

// The picture an image tile draws, at the frame the slider is on. The whole of
// the preview for that kind - which is the point of the kind.
void TileAssetEditor::DrawImagePreview(const eng::TileAsset& asset) {
    const eng::TextureRef& art = asset.ArtTexture();
    if (!asset.HasArt() || !art || art->native == nullptr || art->width <= 0) {
        return;
    }

    const float width      = 96.0f;
    const float frameWidth = static_cast<float>(asset.TileSize()) /
                             static_cast<float>(art->width);
    const float u0         = static_cast<float>(m_previewFrame) * frameWidth;

    ImGui::Image(reinterpret_cast<ImTextureID>(art->native), ImVec2(width, width),
                 ImVec2(u0, 0.0f), ImVec2(u0 + frameWidth, 1.0f));
}

void TileAssetEditor::DrawBandStrip(const eng::TileAsset& asset) {
    const eng::TextureRef& sheet = asset.ArtTexture();
    if (!asset.HasArt() || !sheet || sheet->native == nullptr || sheet->width <= 0) {
        return;
    }

    const float width = 48.0f;
    const float frameWidth = static_cast<float>(asset.TileSize()) /
                             static_cast<float>(sheet->width);
    const float u0 = static_cast<float>(m_previewFrame) * frameWidth;

    ImGui::Image(reinterpret_cast<ImTextureID>(sheet->native),
                 ImVec2(width, width * static_cast<float>(eng::kSheetBands)),
                 ImVec2(u0, 0.0f), ImVec2(u0 + frameWidth, 1.0f));

    ImGui::SameLine();
    ImGui::BeginGroup();
    for (int band = 0; band < eng::kTileSlotCount; ++band) {
        ImGui::TextDisabled("%s", eng::TileSlotName(static_cast<eng::TileSlot>(band)));
        if (band + 1 < eng::kTileSlotCount) {
            ImGui::Dummy(ImVec2(0.0f, width - ImGui::GetTextLineHeight()));
        }
    }
    ImGui::EndGroup();
}

void TileAssetEditor::DrawCombinations(const eng::TileAsset& asset) {
    // ALL FORTY-SEVEN, which is the whole point of showing them: a sheet whose
    // bands are in the wrong order, or whose corners are drawn facing the
    // wrong way, produces tiles that look subtly wrong in a level and obviously
    // wrong here.
    const auto& patterns = eng::AutoTilePatterns();

    const float cell      = 44.0f;
    const float spacing   = ImGui::GetStyle().ItemSpacing.x;
    const float available = ImGui::GetContentRegionAvail().x;
    const int   perRow    = std::max(1, static_cast<int>(available / (cell + spacing)));

    ImDrawList* draw = ImGui::GetWindowDrawList();

    for (std::size_t i = 0; i < patterns.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::Dummy(ImVec2(cell, cell));

        const ImVec2 corner = ImGui::GetItemRectMin();

        // A flat dark square behind each one, so the transparent parts of a
        // corner piece read as shape rather than as whatever happened to be
        // under the panel.
        draw->AddRectFilled(corner, ImVec2(corner.x + cell, corner.y + cell),
                            IM_COL32(26, 28, 34, 255));

        // `combination` is a neighbour arrangement this pattern matches - the
        // bits it requires present, and no others. Every pattern has one, so
        // it is the natural example to draw.
        DrawAssembledTile(draw, asset, patterns[i].combination, corner, cell,
                          m_previewFrame);

        if (ImGui::IsItemHovered()) {
            const eng::QuadrantSlots slots =
                eng::ResolveQuadrants(patterns[i].combination);
            ImGui::SetTooltip("neighbours %d\nTL %s\nTR %s\nBL %s\nBR %s",
                              static_cast<int>(patterns[i].combination),
                              eng::TileSlotName(slots[eng::Quadrant::TopLeft]),
                              eng::TileSlotName(slots[eng::Quadrant::TopRight]),
                              eng::TileSlotName(slots[eng::Quadrant::BottomLeft]),
                              eng::TileSlotName(slots[eng::Quadrant::BottomRight]));
        }

        ImGui::PopID();
        if (static_cast<int>(i + 1) % perRow != 0 && i + 1 < patterns.size()) {
            ImGui::SameLine();
        }
    }
}

void TileAssetEditor::Draw(const std::string& virtualPath,
                           const ComponentBodyDrawer& drawComponentBody) {
    if (virtualPath.empty()) {
        ImGui::TextDisabled("no tile selected");
        return;
    }

    // THE PATH IS THE ARGUMENT NOW, not a brush the panel happened to be
    // holding. Whoever draws this says which tile - the Inspector, from the
    // asset selected in Assets - so there is one answer rather than a second
    // piece of state that could be pointing somewhere else.
    if (virtualPath != m_editPath) {
        LoadEditTarget(virtualPath);
    }

    if (!m_editLoaded) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s",
                           m_editMessage.c_str());
        return;
    }

    // ---- what the engine makes of the CURRENT edits, before saving --------
    //
    // Deserialized fresh every frame from the edit buffer, so the error and
    // the preview below describe what is on screen rather than what is on
    // disk. Typing a wrong sheet path turns the preview red immediately
    // instead of after a save.
    eng::TileAsset    probe;
    std::string       why;
    const bool        valid = probe.Deserialize(m_editJson, why);

    // ---- the fields -------------------------------------------------------
    //
    // WHICH KIND OF ART FIRST, because in a .otile the KEY is the kind: a
    // "sheet" is assembled from five bands and an "image" is drawn whole. See
    // engine/include/engine/tilemap/TileAsset.h.
    bool isImage = m_editJson.contains("image");
    {
        int kind = isImage ? 1 : 0;
        if (ImGui::Combo("Art", &kind,
                         "Sheet - five bands, joins up\0Image - one picture\0")) {
            // THE PATH MOVES ACROSS and the derived numbers are DROPPED. A
            // tileSize worked out for one kind is wrong by a factor of five
            // for the other, and a stale one would fail the load rather than
            // be re-measured. See TileAsset.h on why they are derived.
            const std::string path = isImage ? JsonString(m_editJson, "image", "")
                                             : JsonString(m_editJson, "sheet", "");
            m_editJson.erase("image");
            m_editJson.erase("sheet");
            m_editJson.erase("tileSize");
            m_editJson.erase("frames");

            isImage                                = (kind == 1);
            m_editJson[isImage ? "image" : "sheet"] = path;
            m_editDirty                             = true;
        }
    }

    char artBuffer[256];
    std::snprintf(artBuffer, sizeof(artBuffer), "%s",
                  JsonString(m_editJson, isImage ? "image" : "sheet", "").c_str());
    if (ImGui::InputText(isImage ? "Picture" : "Sheet", artBuffer,
                         sizeof(artBuffer))) {
        m_editJson[isImage ? "image" : "sheet"] = artBuffer;
        m_editDirty                             = true;
    }

    // ======================================================================
    //  DROPPED FROM THE ASSETS PANEL AS WELL AS TYPED - an addition here,
    //  where the Inspector's reference slots are a replacement.
    //
    //  The difference is what is being edited. A component's sprite reference
    //  is a reference, so it shows a NAME and the path is incidental. This is
    //  a FIELD OF A FILE: the panel re-reads its own buffer every frame
    //  precisely so that a wrong path turns red as you type it, and a frame of
    //  a sliced sheet is typed here as "textures/atlas.png#rock". Taking the
    //  text box away would take that with it.
    // ======================================================================
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload(kPayloadTexture)) {
            m_editJson[isImage ? "image" : "sheet"] =
                static_cast<const char*>(payload->Data);
            m_editDirty = true;
        }
        ImGui::EndDragDropTarget();
    }
    if (isImage) {
        ImGui::TextDisabled("one frame of a sliced sheet works here too, "
                            "e.g. textures/atlas.png#rock");
    }

    bool connector = JsonBool(m_editJson, "connector", false);
    if (ImGui::Checkbox("Connector", &connector)) {
        if (connector) {
            m_editJson["connector"] = true;
        } else {
            m_editJson.erase("connector");
        }
        m_editDirty = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(invisible, but its neighbours join through it)");

    // Collision
    {
        const std::string current = JsonString(m_editJson, "collision", "Grid");
        int               index   = (current == "None") ? 0 : 1;
        if (ImGui::Combo("Collision", &index, "None\0Grid\0")) {
            m_editJson["collision"] = (index == 0) ? "None" : "Grid";
            m_editDirty             = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(needs a TilemapCollider on the map too)");
    }

    // Frames
    {
        const std::string current = JsonString(m_editJson, "frameMode", "Single");
        int               index   = (current == "Animation") ? 1
                                    : (current == "Random")  ? 2
                                                             : 0;
        if (ImGui::Combo("Frames", &index, "Single\0Animation\0Random\0")) {
            m_editJson["frameMode"] =
                (index == 1) ? "Animation" : (index == 2) ? "Random" : "Single";
            m_editDirty = true;
        }

        if (index != 0 && probe.FrameCount() <= 1) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                               "this sheet has only one frame, so it will not vary - "
                               "frames go SIDEWAYS");
        }

        if (index == 1) {
            float minSpeed = JsonFloat(m_editJson, "animationMinSpeed", 4.0f);
            float maxSpeed = JsonFloat(m_editJson, "animationMaxSpeed", minSpeed);
            if (ImGui::DragFloat("Slowest", &minSpeed, 0.1f, 0.0f, 60.0f, "%.1f/s")) {
                m_editJson["animationMinSpeed"] = minSpeed;
                m_editDirty                     = true;
            }
            if (ImGui::DragFloat("Fastest", &maxSpeed, 0.1f, 0.0f, 60.0f, "%.1f/s")) {
                m_editJson["animationMaxSpeed"] = maxSpeed;
                m_editDirty                     = true;
            }
            ImGui::TextDisabled("each cell picks its own speed between these, so a "
                                "field does not pulse in unison");
        }
    }

    // ---- connectsTo -------------------------------------------------------
    ImGui::SeparatorText("Joins up with");
    ImGui::TextDisabled("one-way: naming another tile here rounds THIS one off "
                        "against it, not the other way round");

    if (m_editJson.contains("connectsTo") && m_editJson["connectsTo"].is_array()) {
        eng::Json& list = m_editJson["connectsTo"];
        for (std::size_t i = 0; i < list.size();) {
            ImGui::PushID(static_cast<int>(i));
            const std::string entry =
                list[i].is_string() ? list[i].get<std::string>() : "<not text>";

            if (ImGui::SmallButton("x")) {
                list.erase(list.begin() + static_cast<long long>(i));
                m_editDirty = true;
                ImGui::PopID();
                continue;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(entry.c_str());
            ImGui::PopID();
            ++i;
        }
        if (list.empty()) {
            m_editJson.erase("connectsTo");
        }
    }

    if (ImGui::BeginCombo("##addconnect", "add a tile...")) {
        int offered = 0;
        for (const std::string& candidate : KnownTiles()) {
            if (candidate == m_editPath) {
                continue;   // a tile always joins itself; saying so is noise
            }
            bool already = false;
            if (m_editJson.contains("connectsTo") &&
                m_editJson["connectsTo"].is_array()) {
                for (const eng::Json& entry : m_editJson["connectsTo"]) {
                    if (entry.is_string() && entry.get<std::string>() == candidate) {
                        already = true;
                    }
                }
            }
            if (already) {
                continue;
            }
            ++offered;
            if (ImGui::Selectable(candidate.c_str())) {
                if (!m_editJson.contains("connectsTo") ||
                    !m_editJson["connectsTo"].is_array()) {
                    m_editJson["connectsTo"] = eng::Json::array();
                }
                m_editJson["connectsTo"].push_back(candidate);
                m_editDirty = true;
            }
        }
        if (offered == 0) {
            ImGui::TextDisabled("no other tiles to join");
        }
        ImGui::EndCombo();
    }

    // ---- editor tint ------------------------------------------------------
    ImGui::SeparatorText("Editor tint");
    {
        float tint[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        bool  hasTint = false;
        if (m_editJson.contains("editorTint") && m_editJson["editorTint"].is_array() &&
            m_editJson["editorTint"].size() == 4) {
            hasTint = true;
            for (int i = 0; i < 4; ++i) {
                tint[i] = m_editJson["editorTint"][static_cast<std::size_t>(i)]
                              .get<float>() /
                          255.0f;
            }
        }

        if (ImGui::ColorEdit4("Shown in the Scene view only", tint)) {
            m_editJson["editorTint"] = eng::Json::array(
                {static_cast<int>(tint[0] * 255.0f), static_cast<int>(tint[1] * 255.0f),
                 static_cast<int>(tint[2] * 255.0f),
                 static_cast<int>(tint[3] * 255.0f)});
            m_editDirty = true;
        }
        if (hasTint) {
            ImGui::SameLine();
            if (ImGui::SmallButton("clear##tint")) {
                m_editJson.erase("editorTint");
                m_editDirty = true;
            }
        }
    }

    // ---- what the engine concluded, and whether it will load --------------
    ImGui::Separator();
    if (valid) {
        ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.60f, 1.0f),
                           "%dpx tiles, %d frame(s)%s", probe.TileSize(),
                           probe.FrameCount(),
                           probe.IsSolid() ? ", solid" : "");
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", why.c_str());
    }

    ImGui::BeginDisabled(!m_editDirty || !valid);
    if (ImGui::Button("Save")) {
        std::string saveError;
        if (SaveEditTarget(saveError)) {
            m_status = "saved " + m_editPath;
        } else {
            m_status = saveError;
            ENGINE_LOG_ERROR(eng::Channels::kTilemap, "{}", saveError);
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!m_editDirty);
    if (ImGui::Button("Revert")) {
        LoadEditTarget(m_editPath);
    }
    ImGui::EndDisabled();

    if (m_editDirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "unsaved");
    }

    // ---- what a cell painted with this tile becomes -----------------------
    //
    // DRAWN WHETHER OR NOT THE FILE READS. The components are a part of the
    // tile that a bad sheet path has nothing to do with, and a panel that
    // hid them until the art was fixed would be unusable in exactly the
    // moment somebody was building a tile from nothing.
    DrawComponents(drawComponentBody);

    // ---- previews ---------------------------------------------------------
    if (!valid) {
        DrawStatus();
        return;
    }

    if (probe.FrameCount() > 1) {
        if (ImGui::SliderInt("Preview frame", &m_previewFrame, 0,
                             probe.FrameCount() - 1)) {
            m_previewFrame = std::clamp(m_previewFrame, 0, probe.FrameCount() - 1);
        }
    } else {
        m_previewFrame = 0;
    }

    // THE CELL FIRST, because it is the one preview every tile has. An
    // art-less tile used to stop here with "nothing to preview", which was
    // true of its art and quite wrong about the tile: where its collider sits
    // is the only thing there IS to look at on an invisible wall.
    DrawCellPreview(probe);

    if (!probe.HasArt()) {
        ImGui::TextDisabled("this tile has no art, so the cell above is its editor "
                            "tint and nothing else - there are no bands or shapes "
                            "to show.");
        DrawStatus();
        return;
    }

    // NEITHER OF THESE MEANS ANYTHING FOR AN IMAGE TILE, and showing them
    // greyed out would suggest the kind was a limitation rather than a choice.
    // One picture has no bands to be in the wrong order and no shapes to vary,
    // which is said plainly instead.
    if (!probe.IsAutoTile()) {
        ImGui::SeparatorText("The picture");
        DrawImagePreview(probe);
        ImGui::TextDisabled("an image tile looks the same in every cell: there are no "
                            "bands to check and no shapes to vary");
        DrawStatus();
        return;
    }

    ImGui::SeparatorText("The five bands, as drawn");
    DrawBandStrip(probe);

    ImGui::Checkbox("Show all 47 shapes", &m_showCombinations);
    if (m_showCombinations) {
        ImGui::TextDisabled("every arrangement of neighbours this sheet produces - "
                            "hover one to see which band each quarter came from");
        DrawCombinations(probe);
    }

    DrawStatus();
}

// The last thing a Save said. Kept rather than drawn where it happened, so it
// survives the frame the button was pressed in.
void TileAssetEditor::DrawStatus() {
    if (m_status.empty()) {
        return;
    }
    ImGui::Separator();
    ImGui::TextWrapped("%s", m_status.c_str());
}


} // namespace editor
