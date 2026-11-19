// ============================================================================
//  AssetRefs.cpp - the rewrite itself. See AssetRefs.h for the reasoning.
// ============================================================================

#include "AssetRefs.h"

#include <engine/core/Json.h>
#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>
#include <engine/scene/Prefab.h>
#include <engine/scene/Scene.h>
#include <engine/tilemap/TileAsset.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace editor {
namespace {

// ---------------------------------------------------------------------------
//  WHICH FILES ARE WORTH OPENING.
//
//  Every text format this engine stores a path in. A .png is skipped because
//  it cannot contain one, and skipping it is not an optimisation - parsing a
//  few megabytes of image as JSON once per rename would make a rename feel
//  broken.
//
//  .json IS INCLUDED for build.json, which lists the scenes. It is the one
//  file here with no extension of its own.
// ---------------------------------------------------------------------------
bool LooksLikeAnAssetFile(const fs::path& path) {
    const std::string ext = path.extension().string();
    return ext == ".oscene" || ext == ".oprefab" || ext == ".otile" ||
           ext == ".oinput" || ext == ".oanim" || ext == ".oimport" ||
           ext == ".json";
}

// ---------------------------------------------------------------------------
//  THE MATCH. See the header: the old path exactly, or followed by "/" for
//  something inside a moved folder, or by "#" for a frame of a sliced sheet.
//  Anything else sharing a prefix is a different file - "tiles2" is not
//  inside "tiles".
// ---------------------------------------------------------------------------
bool Matches(const std::string& value, const std::string& oldPath) {
    if (value.size() < oldPath.size()) {
        return false;
    }
    if (value.compare(0, oldPath.size(), oldPath) != 0) {
        return false;
    }
    if (value.size() == oldPath.size()) {
        return true;
    }
    const char next = value[oldPath.size()];
    return next == '/' || next == '#';
}

std::string Rewritten(const std::string& value, const std::string& oldPath,
                      const std::string& newPath) {
    return newPath + value.substr(oldPath.size());
}

// Walks a parsed document and rewrites matching strings in place. Returns how
// many it changed.
//
// KEYS ARE NOT TOUCHED, only values. A path used as an object key would be a
// different design than this engine has anywhere, and rewriting keys would
// risk colliding two entries into one.
int RewriteIn(eng::Json& node, const std::string& oldPath,
              const std::string& newPath) {
    int changed = 0;

    if (node.is_string()) {
        const std::string value = node.get<std::string>();
        if (Matches(value, oldPath)) {
            node = Rewritten(value, oldPath, newPath);
            ++changed;
        }
        return changed;
    }
    if (node.is_array()) {
        for (eng::Json& item : node) {
            changed += RewriteIn(item, oldPath, newPath);
        }
        return changed;
    }
    if (node.is_object()) {
        for (auto& entry : node.items()) {
            changed += RewriteIn(entry.value(), oldPath, newPath);
        }
    }
    return changed;
}

} // namespace

RefRewrite RewriteAssetReferences(const std::string& oldPath,
                                  const std::string& newPath) {
    RefRewrite result;
    if (oldPath.empty() || newPath.empty() || oldPath == newPath) {
        return result;
    }

    const std::string& root = eng::FileSystem::AssetRoot();
    if (root.empty()) {
        return result;
    }

    // ---- every asset file under the project ------------------------------
    std::error_code ec;
    for (const auto& item : fs::recursive_directory_iterator(root, ec)) {
        if (ec) {
            break;
        }
        if (!item.is_regular_file(ec) || !LooksLikeAnAssetFile(item.path())) {
            continue;
        }

        // Read through std::filesystem rather than FileSystem::ReadTextFile,
        // because the walk already has a real path in hand and converting it
        // back to a virtual one only to have it resolved again is a round
        // trip that can only lose.
        std::string text;
        {
            std::error_code readEc;
            const auto      size = fs::file_size(item.path(), readEc);
            if (readEc || size == 0) {
                continue;
            }
            FILE* file = nullptr;
#ifdef _WIN32
            if (::_wfopen_s(&file, item.path().c_str(), L"rb") != 0 || file == nullptr) {
                continue;
            }
#else
            file = std::fopen(item.path().string().c_str(), "rb");
            if (file == nullptr) {
                continue;
            }
#endif
            text.resize(static_cast<std::size_t>(size));
            const std::size_t read = std::fread(text.data(), 1, text.size(), file);
            std::fclose(file);
            text.resize(read);
        }

        // CHEAP TEST FIRST. Parsing every scene and prefab in a project to
        // discover that almost none of them mention the file being renamed is
        // the whole cost of this feature; a substring search over the raw
        // text throws out all but a handful before any of them is parsed.
        if (text.find(oldPath) == std::string::npos) {
            continue;
        }

        eng::Json document = eng::Json::parse(text, nullptr, false);
        if (document.is_discarded()) {
            // NOT AN ERROR HERE. A file that will not parse is a problem for
            // whoever owns it, reported where it is loaded. A rename is not
            // the moment to start refusing to proceed over somebody else's
            // broken file - and skipping it is honest, because its references
            // genuinely have not been rewritten.
            ENGINE_LOG_WARN(eng::Channels::kEditor,
                            "'{}' could not be read as JSON, so any references it "
                            "holds were not updated",
                            item.path().string());
            continue;
        }

        const int changed = RewriteIn(document, oldPath, newPath);
        if (changed == 0) {
            continue;
        }

        std::string out = document.dump(2);
        out.push_back('\n');

        FILE* file = nullptr;
#ifdef _WIN32
        if (::_wfopen_s(&file, item.path().c_str(), L"wb") != 0 || file == nullptr) {
            ENGINE_LOG_ERROR(eng::Channels::kEditor,
                             "could not write '{}' while following a rename",
                             item.path().string());
            continue;
        }
#else
        file = std::fopen(item.path().string().c_str(), "wb");
        if (file == nullptr) {
            continue;
        }
#endif
        std::fwrite(out.data(), 1, out.size(), file);
        std::fclose(file);

        ++result.files;
        result.references += changed;
    }

    // ---- the open scene's own filename, if it was the thing that moved ---
    //
    // Not a reference held BY anything - the scene's memory of where it came
    // from. Left alone, the next Ctrl+S writes the level straight back under
    // the old name and the rename looks like it undid itself.
    if (eng::Scene* scene = eng::Scene::Active()) {
        if (Matches(scene->SourcePath(), oldPath)) {
            scene->SetSourcePath(Rewritten(scene->SourcePath(), oldPath, newPath));
        }
    }

    // ---- and the scene being edited, which may hold unsaved changes ------
    //
    // Its file was rewritten above with the others. This is for what is only
    // in memory: saving afterwards would otherwise put the old paths back.
    if (eng::Scene* scene = eng::Scene::Active()) {
        if (scene->IsLoaded()) {
            std::string text;
            std::string error;
            if (scene->SaveToString(text, error) &&
                text.find(oldPath) != std::string::npos) {
                eng::Json document = eng::Json::parse(text, nullptr, false);
                if (!document.is_discarded() &&
                    RewriteIn(document, oldPath, newPath) > 0) {
                    if (scene->LoadFromString(document.dump(2), error)) {
                        result.openScene = true;
                    } else {
                        ENGINE_LOG_ERROR(
                            eng::Channels::kEditor,
                            "the open scene's references could not be updated: {}",
                            error);
                    }
                }
            }
        }
    }

    return result;
}

void ForgetCachedAsset(const std::string& oldPath) {
    if (oldPath.empty()) {
        return;
    }
    eng::PrefabLibrary::Forget(oldPath);
    eng::TileLibrary::Forget(oldPath);
}

std::string DescribeRewrite(const RefRewrite& result) {
    if (result.files == 0 && !result.openScene) {
        return "nothing referred to it";
    }

    std::string text = std::to_string(result.references) + " reference" +
                       (result.references == 1 ? "" : "s") + " in " +
                       std::to_string(result.files) + " file" +
                       (result.files == 1 ? "" : "s");
    if (result.openScene) {
        text += ", and the open scene";
    }
    return text + " updated";
}

} // namespace editor
