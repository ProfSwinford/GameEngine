// ============================================================================
//  EditorAppearance.cpp - see the header for why the style is rebuilt from
//  scratch and why the font waits for a frame boundary.
// ============================================================================

#include "EditorAppearance.h"

#include <engine/core/Log.h>

#include <imgui.h>

#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <unordered_map>

namespace editor::appearance {
namespace {

// ---- what the next frame boundary has to do --------------------------------
//
// Both of these are DEFERRED rather than applied where they are asked for, and
// for two different reasons - see Pump().
std::string g_wantedFontPath;
bool        g_fontPending = false;
float       g_wantedFontSize = 0.0f;
bool        g_sizePending     = false;

// What the atlas is holding, what failed, and why.
std::string g_loadedFontPath;
std::string g_failedFontPath;
std::string g_fontProblem;

// EVERY FONT FILE IS LOADED AT MOST ONCE. AddFontFromFileTTF keeps the whole
// .ttf resident for the life of the atlas and nothing removes it, so without
// this, picking a font, trying another and coming back would hold three font
// files in memory for a font that was only ever one.
std::map<std::string, ImFont*> g_fontsByPath;

void ApplyThemeTo(EditorTheme theme, ImGuiStyle& style) {
    switch (theme) {
        case EditorTheme::Light:   ImGui::StyleColorsLight(&style); break;
        case EditorTheme::Classic: ImGui::StyleColorsClassic(&style); break;
        case EditorTheme::Dark:    ImGui::StyleColorsDark(&style); break;
    }
}

// ============================================================================
//  THE BUILT-IN FONT HAS TO BE Fonts[0] BEFORE ANY OTHER FONT IS ADDED.
//
//  ImGui adds its own font lazily, and only when the atlas is otherwise empty:
//  ImFontAtlasBuildMain does `if (atlas->Sources.Size == 0) AddFontDefault()`,
//  reached from the first NewFrame. Start-up loads the person's font BEFORE
//  the first frame, so without this the custom font becomes Fonts[0] and
//  ImGui never adds the built-in one at all - which makes
//  `io.FontDefault = nullptr`, the way every "go back to the built-in font"
//  path here is spelled, mean "go back to the font we are trying to leave".
// ============================================================================
void EnsureBuiltInFontExists() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->Sources.Size == 0) {
        io.Fonts->AddFontDefault();
    }
}

void FallBackToBuiltIn(const std::string& problem) {
    EnsureBuiltInFontExists();
    ImGui::GetIO().FontDefault = nullptr;
    g_loadedFontPath.clear();
    g_fontProblem = problem;

    if (!problem.empty()) {
        // REMEMBERED AS FAILED, so it is not retried. Apply() runs on every
        // frame of a slider drag, and a font that cannot be loaded would
        // otherwise be opened, rejected and logged sixty times a second.
        g_failedFontPath = g_wantedFontPath;
        ENGINE_LOG_WARN(eng::Channels::kEditor,
                        "the editor font was not loaded: {}; using the built-in one",
                        problem);
    }
}

} // namespace

int ColorCount() {
    return static_cast<int>(ImGuiCol_COUNT);
}

const char* ColorName(int index) {
    if (index < 0 || index >= ColorCount()) {
        return "";
    }
    return ImGui::GetStyleColorName(static_cast<ImGuiCol>(index));
}

int ColorIndex(std::string_view name) {
    // ImGui offers index -> name and not the other way round, so the way back
    // is built once from the way there. Built lazily rather than at start-up
    // because it needs nothing from the context and nobody asks until a
    // preferences file with colours in it is read.
    static const std::unordered_map<std::string, int> kByName = [] {
        std::unordered_map<std::string, int> map;
        for (int i = 0; i < static_cast<int>(ImGuiCol_COUNT); ++i) {
            map.emplace(ImGui::GetStyleColorName(static_cast<ImGuiCol>(i)), i);
        }
        return map;
    }();

    const auto it = kByName.find(std::string(name));
    return (it == kByName.end()) ? -1 : it->second;
}

void ThemeColor(EditorTheme theme, int index, float outRgba[4]) {
    if (outRgba == nullptr) {
        return;
    }
    if (index < 0 || index >= ColorCount()) {
        outRgba[0] = outRgba[1] = outRgba[2] = outRgba[3] = 0.0f;
        return;
    }

    // A STYLE OF ITS OWN, not ImGui::GetStyle(). The question is what the
    // theme says, which is not what the interface is currently showing - the
    // whole point of asking is to compare the two.
    ImGuiStyle plain;
    ApplyThemeTo(theme, plain);

    const ImVec4& colour = plain.Colors[index];
    outRgba[0]           = colour.x;
    outRgba[1]           = colour.y;
    outRgba[2]           = colour.z;
    outRgba[3]           = colour.w;
}

void Apply(const EditorPrefs& prefs) {
    // ======================================================================
    //  FROM A FRESH ImGuiStyle, for the reason in the header: ScaleAllSizes
    //  multiplies what is already there, so scaling the live style compounds
    //  and 1.0 never gets you back.
    // ======================================================================
    ImGuiStyle style;
    ApplyThemeTo(prefs.theme, style);

    // SCALED BEFORE THE COLOURS GO ON, because ScaleAllSizes touches sizes
    // only - the order does not matter to it, and this way the overrides are
    // the last word, which is what the person expects of an override.
    if (prefs.uiScale != 1.0f) {
        style.ScaleAllSizes(prefs.uiScale);
    }

    for (const auto& [name, channels] : prefs.colors) {
        const int index = ColorIndex(name);
        if (index < 0) {
            // A colour this version of ImGui does not have - a preferences
            // file written by a later build, most likely. Kept in the file
            // and ignored here, rather than dropped: downgrading for an
            // afternoon should not cost somebody their theme.
            continue;
        }
        style.Colors[index] =
            ImVec4(channels[0], channels[1], channels[2], channels[3]);
    }

    // ======================================================================
    //  THE SIZE IS WRITTEN HERE AND AGAIN AT THE NEXT FRAME BOUNDARY, and
    //  the second one is the one that counts.
    //
    //  FontSizeBase is NOT a write-only input in 1.92. The context assigns
    //  INTO it from its own copy on every Begin and every End
    //  (UpdateCurrentFontSize, called from SetCurrentWindow), and its own
    //  copy is latched once per frame in UpdateFontsNewFrame. So a value
    //  written to the style in the middle of a frame is overwritten by the
    //  next window that opens, and the editor's font size never changed at
    //  all - it only ever looked right after a restart, because start-up
    //  happens to run before the first frame.
    //
    //  Written in the style as well so that the start-up path, which really
    //  is before any frame, needs no special case.
    // ======================================================================
    style.FontSizeBase  = prefs.fontSize;
    style.FontScaleMain = 1.0f;

    ImGui::GetStyle() = style;

    g_wantedFontSize = prefs.fontSize;
    g_sizePending    = true;

    // The font FILE cannot be loaded here either - see Pump - so it is
    // recorded. A path that has already failed is not re-armed, or a slider
    // drag would retry it on every frame.
    if (prefs.fontPath != g_wantedFontPath ||
        (prefs.fontPath != g_loadedFontPath && prefs.fontPath != g_failedFontPath)) {
        g_wantedFontPath = prefs.fontPath;
        g_fontPending    = true;
    }
}

void Pump() {
    // ---- the font size ---------------------------------------------------
    //
    // Before ImGui::NewFrame, which is what makes it stick: NewFrame latches
    // this into the context, and everything inside the frame reads the latch.
    if (g_sizePending) {
        ImGui::GetStyle().FontSizeBase = g_wantedFontSize;
        g_sizePending                  = false;
    }

    if (!g_fontPending) {
        return;
    }
    g_fontPending = false;

    ImGuiIO& io = ImGui::GetIO();

    // ---- back to the built-in font ---------------------------------------
    if (g_wantedFontPath.empty()) {
        EnsureBuiltInFontExists();
        io.FontDefault = nullptr;
        g_loadedFontPath.clear();
        g_failedFontPath.clear();
        g_fontProblem.clear();
        return;
    }

    if (g_wantedFontPath == g_loadedFontPath || g_wantedFontPath == g_failedFontPath) {
        return;
    }

    // ---- one we already have ---------------------------------------------
    if (const auto it = g_fontsByPath.find(g_wantedFontPath); it != g_fontsByPath.end()) {
        io.FontDefault   = it->second;
        g_loadedFontPath = g_wantedFontPath;
        g_fontProblem.clear();
        return;
    }

    EnsureBuiltInFontExists();

    // ======================================================================
    //  TWO GUARDS, BECAUSE THERE ARE TWO WAYS TO FAIL AND THEY ARE DIFFERENT.
    //
    //  A font path is remembered between sessions, so "the font I chose last
    //  month has been uninstalled" is an ordinary Tuesday and must not be a
    //  crash.
    //
    //   * THE FILE IS NOT THERE. Caught here, by name, with a message that
    //     says which file.
    //
    //   * THE FILE IS THERE AND IS NOT A FONT - half-downloaded, renamed
    //     from something else. AddFontFromFileTTF opens it, hands it to
    //     stb_truetype, and asserts. ImFontFlags_NoLoadError does NOT cover
    //     this: in 1.92 that flag is tested only on the could-not-open path,
    //     which is the one already handled above. The assert is a recoverable
    //     one, so it is the recovery assert that has to be switched off for
    //     the length of the call - after which the null return is the answer.
    // ======================================================================
    std::error_code ec;
    if (!std::filesystem::is_regular_file(std::filesystem::path(g_wantedFontPath), ec)) {
        FallBackToBuiltIn("there is no file at '" + g_wantedFontPath + "'");
        return;
    }

    ImFontConfig config;
    config.Flags |= ImFontFlags_NoLoadError;

    const bool assertsWere             = io.ConfigErrorRecoveryEnableAssert;
    io.ConfigErrorRecoveryEnableAssert = false;
    ImFont* font = io.Fonts->AddFontFromFileTTF(g_wantedFontPath.c_str(), 0.0f, &config);
    io.ConfigErrorRecoveryEnableAssert = assertsWere;

    if (font == nullptr) {
        FallBackToBuiltIn("'" + g_wantedFontPath +
                          "' is not a font this editor can read");
        return;
    }

    g_fontsByPath[g_wantedFontPath] = font;
    io.FontDefault                  = font;
    g_loadedFontPath                = g_wantedFontPath;
    g_failedFontPath.clear();
    g_fontProblem.clear();
    ENGINE_LOG_INFO(eng::Channels::kEditor, "editor font loaded from '{}'",
                    g_loadedFontPath);
}

const std::string& FontProblem() {
    return g_fontProblem;
}

EditorPrefs& Current() {
    static EditorPrefs prefs;
    return prefs;
}

void LoadAndApply() {
    const bool hadFile = LoadPrefs(Current());
    Apply(Current());

    // Done at once rather than at the next frame boundary, because start-up
    // IS a frame boundary - no frame has begun yet - and waiting would mean
    // one frame drawn in the wrong font.
    Pump();

    if (hadFile) {
        ENGINE_LOG_INFO(eng::Channels::kEditor, "editor appearance read from '{}'",
                        PrefsFilePath());
    }
}

bool Commit(std::string& outError) {
    Apply(Current());
    return SavePrefs(Current(), outError);
}

} // namespace editor::appearance
