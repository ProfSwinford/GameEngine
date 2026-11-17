#include "TilePaint.h"

#include "EditorApp.h"

#include <imgui.h>

namespace editor {

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

TilePaintState& TilePaintState::Get() {
    static TilePaintState state;
    return state;
}

eng::TilemapComponent* TilePaintState::TargetMap() const {
    eng::Scene* scene = eng::Scene::Active();
    if (scene == nullptr) {
        return nullptr;
    }

    // THE SELECTION IS THE TARGET. See the note at the top of the header: the
    // palette being looked at is the selected entity's, so the map being
    // painted has to be the same one or the two would disagree.
    eng::Entity* entity = scene->Get(EditorState::Get().selected);
    if (entity == nullptr) {
        return nullptr;
    }
    return entity->Find<eng::TilemapComponent>();
}

// ---------------------------------------------------------------------------

void DrawTilePreview(const eng::TileAsset& asset, float size) {
    const eng::TextureRef& sheet = asset.ArtTexture();
    if (!asset.HasArt() || !sheet || sheet->native == nullptr ||
        sheet->width <= 0 || sheet->height <= 0) {
        // No art at all, so it gets its editor tint as a swatch - which is the
        // same thing it draws in the Scene view.
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
    // way down. Frame 0 is the leftmost column. An image tile has one band and
    // starts at the top.
    const float frameWidth = static_cast<float>(asset.TileSize()) /
                             static_cast<float>(sheet->width);
    const float top        = asset.IsAutoTile()
                                 ? 4.0f / static_cast<float>(eng::kSheetBands)
                                 : 0.0f;
    const ImVec2 uv0(0.0f, top);
    const ImVec2 uv1(frameWidth, 1.0f);

    ImGui::Image(reinterpret_cast<ImTextureID>(sheet->native), ImVec2(size, size),
                 uv0, uv1);
}

std::string TileShortName(const std::string& virtualPath) {
    const std::size_t slash = virtualPath.find_last_of("/\\");
    std::string       name  = (slash == std::string::npos)
                                  ? virtualPath
                                  : virtualPath.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) {
        name.erase(dot);
    }
    return name;
}

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

} // namespace editor
