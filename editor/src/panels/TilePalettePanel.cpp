#include "TilePalettePanel.h"

#include "../EditorApp.h"

#include <imgui.h>

#include <algorithm>

namespace editor {

namespace {

// Every .otile under assets/, found by walking the whole tree.
//
// FileSystem::ListFiles reads ONE folder, and tiles are organised into
// sub-folders like everything else, so the walk is done here. It happens on
// demand rather than every frame - see the Rescan button.
void CollectTiles(const std::string& directory, std::vector<std::string>& out) {
    std::vector<eng::FileSystem::DirEntry> entries;
    if (!eng::FileSystem::ListDirectory(directory, entries)) {
        return;
    }

    for (const eng::FileSystem::DirEntry& entry : entries) {
        if (entry.isDirectory) {
            CollectTiles(entry.virtualPath, out);
        } else if (eng::FileSystem::HasExtension(entry.virtualPath, ".otile")) {
            out.push_back(entry.virtualPath);
        }
    }
}

// The last part of a path, without its extension: "tiles/grass.otile" -> "grass".
std::string ShortName(const std::string& virtualPath) {
    const std::size_t slash = virtualPath.find_last_of("/\\");
    std::string name = (slash == std::string::npos) ? virtualPath
                                                    : virtualPath.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) {
        name.erase(dot);
    }
    return name;
}

// A preview of one tile: the Fill band, which is the tile as it looks in the
// middle of a solid area and so the one that reads as "this material".
void DrawTilePreview(const eng::TileAsset& asset, float size) {
    const eng::TextureRef& sheet = asset.Sheet();
    if (!asset.HasSheet() || !sheet || sheet->native == nullptr ||
        sheet->width <= 0 || sheet->height <= 0) {
        // A connector has no art at all, so it gets its editor tint as a
        // swatch - which is the same thing it draws in the Scene view.
        const eng::Color tint = asset.EditorTint();
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImVec4(tint.r / 255.0f, tint.g / 255.0f,
                                     tint.b / 255.0f, 1.0f));
        ImGui::Button(asset.IsConnector() ? "link##preview" : "?##preview",
                      ImVec2(size, size));
        ImGui::PopStyleColor();
        return;
    }

    // The Fill band is the LAST of the five, so it starts four fifths of the
    // way down. Frame 0 is the leftmost column.
    const float frameWidth = static_cast<float>(asset.TileSize()) /
                             static_cast<float>(sheet->width);
    const ImVec2 uv0(0.0f, 4.0f / static_cast<float>(eng::kSheetBands));
    const ImVec2 uv1(frameWidth, 1.0f);

    ImGui::Image(reinterpret_cast<ImTextureID>(sheet->native), ImVec2(size, size),
                 uv0, uv1);
}

} // namespace

const char* TileToolName(TileTool tool) {
    switch (tool) {
        case TileTool::Select:    return "Select";
        case TileTool::Paint:     return "Paint";
        case TileTool::Erase:     return "Erase";
        case TileTool::Rectangle: return "Rectangle";
        case TileTool::Fill:      return "Fill";
        case TileTool::Pick:      return "Pick";
    }
    return "Select";
}

// ---------------------------------------------------------------------------
//  TilePaintState
// ---------------------------------------------------------------------------

TilePaintState& TilePaintState::Get() {
    static TilePaintState state;
    return state;
}

eng::TilemapComponent* TilePaintState::TargetMap() const {
    eng::Scene* scene = eng::Scene::Active();
    if (scene == nullptr) {
        return nullptr;
    }
    eng::Entity* entity = scene->Get(tilemap);
    if (entity == nullptr) {
        return nullptr;
    }
    return entity->Find<eng::TilemapComponent>();
}

bool TilePaintState::EnsureTarget() {
    if (TargetMap() != nullptr) {
        return true;
    }

    eng::Scene* scene = eng::Scene::Active();
    if (scene == nullptr) {
        return false;
    }

    // The selected entity first, because if you have just clicked on a map
    // that is obviously the one you meant.
    if (eng::Entity* selected = scene->Get(EditorState::Get().selected);
        selected != nullptr && selected->Find<eng::TilemapComponent>() != nullptr) {
        tilemap = selected->Id();
        return true;
    }

    // Otherwise the only one in the scene - but ONLY if there is exactly one.
    // Picking the first of several would be a guess, and a guess here means
    // painting into a map you cannot see.
    eng::EntityId found{};
    int           count = 0;
    scene->ForEach([&](eng::Entity& entity) {
        if (entity.Find<eng::TilemapComponent>() != nullptr) {
            ++count;
            found = entity.Id();
        }
    });

    if (count == 1) {
        tilemap = found;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
//  The panel
// ---------------------------------------------------------------------------

void TilePalettePanel::Draw() {
    if (!m_scanned) {
        RescanTiles();
    }

    TilePaintState& state = TilePaintState::Get();
    state.EnsureTarget();

    DrawToolRow();
    ImGui::Separator();
    DrawTargetRow();
    ImGui::Separator();
    DrawTileGrid();

    // Collapsed by default: most of the time this panel is for choosing a
    // brush, and a tile's settings are a different job that happens rarely.
    // The Assets panel opens it by double-clicking a .otile.
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Edit tile")) {
        DrawTileEditor();
    }

    if (m_status[0] != '\0') {
        ImGui::Separator();
        ImGui::TextWrapped("%s", m_status);
    }
}

void TilePalettePanel::DrawToolRow() {
    TilePaintState& state = TilePaintState::Get();

    const TileTool tools[] = {TileTool::Select, TileTool::Paint, TileTool::Erase,
                              TileTool::Rectangle, TileTool::Fill, TileTool::Pick};

    for (std::size_t i = 0; i < std::size(tools); ++i) {
        if (i != 0) {
            ImGui::SameLine();
        }
        const bool active = (state.tool == tools[i]);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button(TileToolName(tools[i]))) {
            state.tool = tools[i];
        }
        if (active) {
            ImGui::PopStyleColor();
        }
    }

    switch (state.tool) {
        case TileTool::Select:
            ImGui::TextDisabled("the Scene view behaves normally; pick a tool to paint");
            break;
        case TileTool::Paint:
            ImGui::TextDisabled("left-drag paints, right-drag erases");
            break;
        case TileTool::Erase:
            ImGui::TextDisabled("left-drag erases");
            break;
        case TileTool::Rectangle:
            ImGui::TextDisabled("drag a box; right-drag clears one");
            break;
        case TileTool::Fill:
            ImGui::TextDisabled("fills the connected area of whatever you click");
            break;
        case TileTool::Pick:
            ImGui::TextDisabled("click a cell to make its tile the chosen one");
            break;
    }

    ImGui::Checkbox("Show grid", &state.showGrid);

    if (state.IsPainting()) {
        ImGui::SameLine();

        // IT USED TO SAY "no undo - erase to correct", which was true when it
        // was written. Ctrl+Z now covers painting, because a tilemap's cells
        // are part of the scene document and undo restores the whole document
        // - see editor/src/UndoStack.h. One stroke is one step: the snapshot
        // is taken when the mouse comes up, not per cell.
        ImGui::TextDisabled("Ctrl+Z undoes a whole stroke");
    }
}

void TilePalettePanel::DrawTargetRow() {
    TilePaintState& state = TilePaintState::Get();
    eng::Scene*     scene = eng::Scene::Active();
    if (scene == nullptr) {
        ImGui::TextDisabled("no scene loaded");
        return;
    }

    eng::TilemapComponent* map = state.TargetMap();

    std::string label = "(none - painting will do nothing)";
    if (map != nullptr) {
        if (const eng::Entity* owner = map->Owner(); owner != nullptr) {
            label = owner->Name();
        }
    }

    if (ImGui::BeginCombo("Painting into", label.c_str())) {
        int offered = 0;
        scene->ForEach([&](eng::Entity& entity) {
            if (entity.Find<eng::TilemapComponent>() == nullptr) {
                return;
            }
            ++offered;
            const bool chosen = (entity.Id() == state.tilemap);
            if (ImGui::Selectable(entity.Name().c_str(), chosen)) {
                state.tilemap = entity.Id();
            }
        });
        if (offered == 0) {
            ImGui::TextDisabled("this scene has no tilemaps");
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button("New Tilemap entity")) {
        // ---- A UNIQUE NAME, WHICH IS NOT A TIDINESS POINT -----------------
        //
        // This used to be CreateEntity("Tilemap") flat, so pressing this
        // button twice produced two entities both called "Tilemap" - and
        // Scene::CreateEntity does not de-duplicate, it just overwrites the
        // name index.
        //
        // A NAME IS AN IDENTITY in this engine, not a label. Scene::Find and
        // the name index answer for one of the two; a script's entity
        // reference is a name (EntityRef.h); Scene::ResolveParents matches
        // parents BY NAME on every load, so the first map's children would be
        // reparented onto the second one every time the scene was read back;
        // and undo restores the selection and the tile brush's target by name
        // (UndoStack.h). Destroying either one then erases the shared index
        // entry and Find stops answering for the survivor.
        //
        // Every other creation path in the editor already goes through
        // MakeUniqueName. This was the one that did not.
        const std::string name = scene->MakeUniqueName("Tilemap");

        eng::Entity* entity = scene->Get(scene->CreateEntity(name));
        if (entity != nullptr) {
            entity->AddComponent(eng::TilemapComponent::kTypeName);

            // A collider too. A map with no collider is the commonest "why can
            // I walk through my wall?", and adding one here costs nothing -
            // only cells whose TILE says it is solid become solid.
            entity->AddComponent(eng::TilemapColliderComponent::kTypeName);

            state.tilemap = entity->Id();
            EditorState::Get().SelectOnly(entity->Id());
            EditorState::Get().MarkDirty();

            // NAMES WHAT IT ACTUALLY MADE. It used to say 'Tilemap' whatever
            // the entity ended up being called, which was true exactly once.
            std::snprintf(m_status, sizeof(m_status),
                          "created '%s' with a TilemapComponent and a "
                          "TilemapColliderComponent", name.c_str());
        }
    }

    if (map != nullptr) {
        ImGui::SameLine();
        if (ImGui::Button("Tidy palette")) {
            const std::size_t dropped = map->CompactPalette();
            EditorState::Get().MarkDirty();
            std::snprintf(m_status, sizeof(m_status),
                          "dropped %zu unused palette entr%s", dropped,
                          dropped == 1 ? "y" : "ies");
        }
        DrawMapStats(*map);
    }
}

void TilePalettePanel::DrawMapStats(eng::TilemapComponent& map) {
    ImGui::Text("%zu tile(s) in %zu chunk(s), %zu material(s)", map.TileCount(),
                map.ChunkCount(), map.Palette().size());

    const eng::Vec2 cell = map.CellSize();
    ImGui::TextDisabled("cells are %.0f x %.0f units%s", cell.x, cell.y,
                        map.HasExplicitCellSize() ? "" : " (from the tile's own size)");

    // What the last frame actually drew. The two ADD UP to every non-empty
    // cell in the scene's maps, so "my tilemap is invisible" has an answer
    // here: nothing drawn and plenty skipped means the camera is not looking
    // at it, while nothing of either means there is nothing painted.
    ImGui::TextDisabled("last frame: %zu cell(s) drawn, %zu skipped",
                        eng::TilemapRenderSystem::CellsDrawn(),
                        eng::TilemapRenderSystem::CellsSkipped());

    eng::Entity* owner = map.Owner();
    if (owner == nullptr) {
        return;
    }

    if (auto* collider = owner->Find<eng::TilemapColliderComponent>()) {
        const std::size_t cells = collider->SolidCellCount();
        const std::size_t boxes = collider->BoxCount();

        // The merge ratio, which is the number worth showing: it is the
        // difference between a flat floor that is one box and a flat floor
        // that is two hundred boxes with a join between every pair.
        if (boxes > 0) {
            ImGui::Text("collider: %zu solid cell(s) merged into %zu box(es)  (%.0fx)",
                        cells, boxes,
                        static_cast<double>(cells) / static_cast<double>(boxes));
        } else {
            ImGui::TextDisabled("collider: nothing solid yet");
        }
    } else {
        ImGui::TextDisabled("no TilemapColliderComponent: nothing here is solid");
    }
}

void TilePalettePanel::DrawTileGrid() {
    TilePaintState& state = TilePaintState::Get();

    if (ImGui::Button("Rescan")) {
        RescanTiles();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu .otile file(s)", m_tiles.size());

    if (m_tiles.empty()) {
        ImGui::TextWrapped(
            "No tile assets found. A tile is a .otile file naming a sheet that is "
            "ONE TILE WIDE and FIVE TILES TALL - see assets/tiles/stone.otile for "
            "the smallest useful example.");
        return;
    }

    const float  preview   = 48.0f;
    const float  available = ImGui::GetContentRegionAvail().x;
    const float  step      = preview + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    const int    perRow    = std::max(1, static_cast<int>(available / step));

    int column = 0;
    for (const std::string& path : m_tiles) {
        const eng::TileAsset* asset = eng::TileLibrary::Load(path);

        ImGui::PushID(path.c_str());
        ImGui::BeginGroup();

        const bool chosen = (state.tilePath == path);
        if (chosen) {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.95f, 0.75f, 0.35f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
        }

        if (asset != nullptr) {
            // An invisible button OVER the preview, so the whole square is
            // clickable rather than just a caption underneath it.
            const ImVec2 corner = ImGui::GetCursorScreenPos();
            DrawTilePreview(*asset, preview);
            ImGui::SetCursorScreenPos(corner);
            if (ImGui::InvisibleButton("##pick", ImVec2(preview, preview))) {
                state.tilePath = path;

                // Choosing a tile is almost always followed by painting with
                // it, so Select steps aside. Any other tool is left alone -
                // somebody who chose Rectangle meant it.
                if (state.tool == TileTool::Select) {
                    state.tool = TileTool::Paint;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\n%dpx, %d frame(s), %s\ncollision: %s%s",
                                  path.c_str(), asset->TileSize(),
                                  asset->FrameCount(),
                                  eng::TileFrameModeName(asset->FrameMode()),
                                  eng::TileCollisionName(asset->Collision()),
                                  asset->IsConnector() ? "\nconnector: invisible, but "
                                                         "its neighbours join through it"
                                                       : "");
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button("ERR", ImVec2(preview, preview))) {
                std::snprintf(m_status, sizeof(m_status),
                              "'%s' would not load - the Console says why",
                              path.c_str());
            }
            ImGui::PopStyleColor();
        }

        if (chosen) {
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }

        const std::string name = ShortName(path);
        ImGui::TextDisabled("%.9s", name.c_str());

        ImGui::EndGroup();
        ImGui::PopID();

        if (++column % perRow != 0) {
            ImGui::SameLine();
        }
    }
    ImGui::NewLine();
}

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
    const eng::TextureRef& sheet = asset.Sheet();
    if (!asset.HasSheet() || !sheet || sheet->native == nullptr ||
        sheet->width <= 0 || sheet->height <= 0) {
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

bool TilePalettePanel::LoadEditTarget(const std::string& virtualPath) {
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
    return true;
}

bool TilePalettePanel::SaveEditTarget(std::string& outError) {
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

void TilePalettePanel::RefreshTilemapsUsing(const std::string& otilePath) {
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

void TilePalettePanel::DrawBandStrip(const eng::TileAsset& asset) {
    const eng::TextureRef& sheet = asset.Sheet();
    if (!asset.HasSheet() || !sheet || sheet->native == nullptr || sheet->width <= 0) {
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

void TilePalettePanel::DrawCombinations(const eng::TileAsset& asset) {
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

void TilePalettePanel::DrawTileEditor() {
    TilePaintState& state = TilePaintState::Get();

    if (state.tilePath.empty()) {
        ImGui::TextDisabled("choose a tile above to edit it");
        return;
    }

    if (state.tilePath != m_editPath) {
        LoadEditTarget(state.tilePath);
    }

    ImGui::TextDisabled("%s", m_editPath.c_str());

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
    char sheetBuffer[256];
    std::snprintf(sheetBuffer, sizeof(sheetBuffer), "%s",
                  JsonString(m_editJson, "sheet", "").c_str());
    if (ImGui::InputText("Sheet", sheetBuffer, sizeof(sheetBuffer))) {
        m_editJson["sheet"] = sheetBuffer;
        m_editDirty         = true;
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
        for (const std::string& candidate : m_tiles) {
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
            std::snprintf(m_status, sizeof(m_status), "saved %s", m_editPath.c_str());
        } else {
            std::snprintf(m_status, sizeof(m_status), "%s", saveError.c_str());
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

    // ---- previews ---------------------------------------------------------
    if (!valid) {
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

    if (!probe.HasSheet()) {
        ImGui::TextDisabled("a connector has no art, so there is nothing to preview");
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
}

void TilePalettePanel::RescanTiles() {
    m_tiles.clear();
    CollectTiles("", m_tiles);
    std::sort(m_tiles.begin(), m_tiles.end());
    m_scanned = true;
}

} // namespace editor
