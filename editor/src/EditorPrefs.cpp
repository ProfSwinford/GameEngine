// ============================================================================
//  EditorPrefs.cpp - the editor's appearance, as a file. See the header for
//  why it is not in the project and why it knows nothing about ImGui.
// ============================================================================

#include "EditorPrefs.h"

#include <engine/fs/UserPaths.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace editor {
namespace {

constexpr const char* kFileName = "editor-prefs.json";

// What the interface can actually be drawn with. Not taste - a 2px font is
// not small text, it is an editor nobody can use and no menu to fix it from.
constexpr float kMinFontSize = 7.0f;
constexpr float kMaxFontSize = 48.0f;
constexpr float kMinUiScale  = 0.5f;
constexpr float kMaxUiScale  = 3.0f;

const char* ThemeName(EditorTheme theme) {
    switch (theme) {
        case EditorTheme::Light:   return "light";
        case EditorTheme::Classic: return "classic";
        case EditorTheme::Dark:    break;
    }
    return "dark";
}

EditorTheme ThemeFromName(const std::string& name) {
    if (name == "light") {
        return EditorTheme::Light;
    }
    if (name == "classic") {
        return EditorTheme::Classic;
    }
    // Including anything unrecognised. A file from a later version that names
    // a theme this one has never heard of costs you the theme, not the editor.
    return EditorTheme::Dark;
}

// A number from the file, or the one already there. Deliberately silent: see
// the header - a value that makes no sense is replaced, not reported.
float ReadNumber(const eng::Json& document, const char* key, float fallback) {
    if (document.contains(key) && document[key].is_number()) {
        return document[key].get<float>();
    }
    return fallback;
}

} // namespace

void ClampPrefs(EditorPrefs& prefs) {
    prefs.fontSize = std::clamp(prefs.fontSize, kMinFontSize, kMaxFontSize);
    prefs.uiScale  = std::clamp(prefs.uiScale, kMinUiScale, kMaxUiScale);

    for (auto& entry : prefs.colors) {
        for (float& channel : entry.second) {
            channel = std::clamp(channel, 0.0f, 1.0f);
        }
    }
}

EditorPrefs PrefsFromJson(const eng::Json& document) {
    EditorPrefs prefs;
    if (!document.is_object()) {
        return prefs;
    }

    if (document.contains("theme") && document["theme"].is_string()) {
        prefs.theme = ThemeFromName(document["theme"].get<std::string>());
    }
    prefs.fontSize = ReadNumber(document, "fontSize", prefs.fontSize);
    prefs.uiScale  = ReadNumber(document, "uiScale", prefs.uiScale);

    if (document.contains("fontPath") && document["fontPath"].is_string()) {
        prefs.fontPath = document["fontPath"].get<std::string>();
    }

    if (document.contains("colors") && document["colors"].is_object()) {
        for (const auto& [name, value] : document["colors"].items()) {
            // FOUR NUMBERS OR NOTHING. A three-entry colour is not an opaque
            // colour with the alpha left out, it is a file somebody has been
            // editing by hand and got wrong, and guessing at the missing one
            // is how a half-typed line becomes an invisible window.
            if (!value.is_array() || value.size() != 4) {
                continue;
            }
            std::array<float, 4> channels{};
            bool                 ok = true;
            for (std::size_t i = 0; i < 4; ++i) {
                if (!value[i].is_number()) {
                    ok = false;
                    break;
                }
                channels[i] = value[i].get<float>();
            }
            if (ok && !name.empty()) {
                prefs.colors[name] = channels;
            }
        }
    }

    ClampPrefs(prefs);
    return prefs;
}

eng::Json PrefsToJson(const EditorPrefs& prefs) {
    eng::Json document = eng::Json::object();

    document["_comment"] =
        "How the editor looks, for this user on this machine. Delete this file to "
        "go back to the defaults. It is NOT part of any project and is never "
        "shipped - a project's own settings are in its config/engine.json.";

    document["theme"]    = ThemeName(prefs.theme);
    document["fontSize"] = prefs.fontSize;
    document["uiScale"]  = prefs.uiScale;
    document["fontPath"] = prefs.fontPath;

    // Written even when empty, so that the shape of the file is the same
    // whether or not anybody has changed a colour - a person opening it to
    // edit one by hand can see where they go.
    eng::Json colors = eng::Json::object();
    for (const auto& [name, channels] : prefs.colors) {
        colors[name] = eng::Json::array({channels[0], channels[1], channels[2],
                                         channels[3]});
    }
    document["colors"] = std::move(colors);

    return document;
}

std::string PrefsFilePath() {
    return eng::UserPaths::File(kFileName);
}

bool LoadPrefs(EditorPrefs& outPrefs) {
    outPrefs = EditorPrefs{};

    const std::string path = PrefsFilePath();
    if (path.empty()) {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        // No file yet, or a home directory we cannot read. Both mean the
        // defaults, which is exactly what outPrefs already holds.
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    const eng::Json document = eng::Json::parse(buffer.str(), nullptr, false);
    if (document.is_discarded()) {
        // Left strictly alone rather than overwritten: somebody is editing it
        // by hand and has a half-typed line in there. The editor opens with
        // the defaults and the next Save is what replaces it - by which point
        // they will have noticed.
        return false;
    }

    outPrefs = PrefsFromJson(document);
    return true;
}

bool SavePrefs(const EditorPrefs& prefs, std::string& outError) {
    const std::string path = PrefsFilePath();
    if (path.empty()) {
        outError = "this machine has no settings folder to write to";
        return false;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        outError = "could not write '" + path + "'";
        return false;
    }

    const std::string text = PrefsToJson(prefs).dump(2) + "\n";
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) {
        outError = "writing '" + path + "' failed";
        return false;
    }

    outError.clear();
    return true;
}

bool ForgetPrefs(std::string& outError) {
    const std::string path = PrefsFilePath();
    if (path.empty()) {
        outError = "this machine has no settings folder";
        return false;
    }

    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(path), ec);
    if (ec) {
        outError = "could not delete '" + path + "': " + ec.message();
        return false;
    }

    outError.clear();
    return true;
}

} // namespace editor
