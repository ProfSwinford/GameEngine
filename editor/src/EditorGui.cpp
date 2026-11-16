// ============================================================================
//  EditorGui.cpp - starting, running and stopping Dear ImGui. See EditorGui.h.
//
//  This is the only file in the editor that includes SDL, and it only does so
//  because ImGui's backends are written against it.
// ============================================================================

#include "EditorGui.h"

#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>
#include <engine/platform/Window.h>
#include <engine/tools/GuiHooks.h>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_internal.h>          // DockBuilder, used for the default layout
#include <imgui_impl_sdl3.h>         // the input half of ImGui's SDL support
#include <imgui_impl_sdlrenderer3.h> // the drawing half

namespace editor {
namespace {

bool          g_initialised = false;
SDL_Renderer* g_renderer    = nullptr;
bool          g_gameFocus   = false;

// The Game view's picture, in window pixels, as it was drawn LAST frame, and
// whether the game was live in it. Published by GamePanel::Draw and read by
// HookWantsMouse. All zero means "there is no game view to click into", which
// is what a closed, collapsed or background-tabbed panel has to look like -
// see GamePanel::OnHidden.
float         g_gameX       = 0.0f;
float         g_gameY       = 0.0f;
float         g_gameW       = 0.0f;
float         g_gameH       = 0.0f;
bool          g_gameLive    = false;
bool          g_layoutBuilt = false;

// Moves a pre-existing imgui.ini to the place the editor now keeps it.
//
// ONE-TIME AND BEST-EFFORT. It runs only when there is no layout in the new
// place and one in an old place, so it cannot overwrite anything, and every
// failure is ignored: the worst outcome is the panels starting in their
// default arrangement, which is not worth a message on the way up.
//
// Two old places are looked at, because those are the two directories the
// editor gets launched from: the project root, and the folder the executable
// sits in. Whichever is found first is copied.
void AdoptLegacyIniIfPresent(const std::string& newPath) {
    std::error_code ec;
    if (std::filesystem::exists(newPath, ec)) {
        return;
    }

    std::vector<std::filesystem::path> candidates;
    candidates.emplace_back(std::filesystem::path(eng::FileSystem::AssetRoot()) /
                            "imgui.ini");
    if (const char* base = SDL_GetBasePath(); base != nullptr && base[0] != '\0') {
        candidates.emplace_back(std::filesystem::path(base) / "imgui.ini");
    }

    for (const std::filesystem::path& old : candidates) {
        if (!std::filesystem::exists(old, ec)) {
            continue;
        }
        std::filesystem::create_directories(
            std::filesystem::path(newPath).parent_path(), ec);
        std::filesystem::copy_file(old, newPath,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) {
            ENGINE_LOG_INFO(eng::Channels::kEditor,
                            "moved your panel layout from '{}' to '{}'", old.string(),
                            newPath);
        }
        return;
    }
}

// ---------------------------------------------------------------------------
//  The three functions the engine calls to find out whether the interface
//  claimed an input event. They are plain functions rather than members so
//  their addresses fit the function pointers in eng::GuiHooks.
// ---------------------------------------------------------------------------

bool HookProcessEvent(const void* platformEvent) {
    if (!g_initialised || platformEvent == nullptr) {
        return false;
    }
    return ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(platformEvent));
}

bool HookWantsKeyboard() {
    if (!g_initialised) {
        return false;
    }
    // While the Game view has focus the editor gives up the keyboard entirely,
    // so key presses stop being marked as claimed and reach the game.
    if (g_gameFocus) {
        return false;
    }
    return ImGui::GetIO().WantCaptureKeyboard;
}

// ===========================================================================
//  THE MOUSE IS HANDED OVER BY PLACE, WHERE THE KEYBOARD IS HANDED OVER BY
//  FOCUS - and that difference is the whole of this function.
//
//  A key press has no location, so "who is typing" is the only question and
//  focus answers it. A click has one, and the right question is "is this
//  point inside the running game?" Focus would be the wrong test even though
//  it is the one used above: with the Game view focused, clicking the
//  Toolbar's Stop button is a click the editor does NOT want given away, and
//  the game would fire a shot on its way to being stopped.
//
//  AGAINST THE EVENT'S OWN POSITION, never ImGui's cursor. See the note on
//  GuiHooks::WantsMouse - this rectangle was latched during the last frame's
//  draw, but a docked panel cannot move while a click is being delivered,
//  whereas the cursor can cross the whole window during one long frame. The
//  stale fact here is the one that does not change.
//
//  AND ONLY WHILE THE GAME IS LIVE, which carries ImGui's own hover test -
//  see GamePanel::Draw. That is what stops a modal, a popup or a
//  drag-and-drop hovering over the view from having its clicks swallowed by
//  the game underneath it. ImGui already answers that correctly, and
//  re-deriving it from a rectangle would not.
// ===========================================================================
bool HookWantsMouse(float windowX, float windowY) {
    if (!g_initialised) {
        return false;
    }
    if (g_gameLive && windowX >= g_gameX && windowY >= g_gameY &&
        windowX < g_gameX + g_gameW && windowY < g_gameY + g_gameH) {
        return false;
    }
    return ImGui::GetIO().WantCaptureMouse;
}

} // namespace

bool EditorGui::Init(eng::Window& window) {
    if (g_initialised) {
        return true;
    }
    if (!window.IsValid()) {
        ENGINE_LOG_ERROR(eng::Channels::kEditor,
                         "the editor interface was given a window that failed to open");
        return false;
    }

    IMGUI_CHECKVERSION();
    if (ImGui::CreateContext() == nullptr) {
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not create the ImGui context");
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();

    // ---- WHERE THE PANEL LAYOUT IS REMEMBERED -----------------------------
    //
    // ImGui's default is the bare name "imgui.ini", which it opens relative to
    // the CURRENT DIRECTORY. That means the file lands wherever the editor
    // happened to be launched from: one copy beside the executable when you
    // run it from build/debug/bin, another in the project root when you run it
    // from there, and the arrangement you get depends on which. Rearranging
    // the panels and finding them moved back the next day is the symptom, and
    // nothing about it points at the cause.
    //
    // Resolved through FileSystem instead, so it is the same file every time
    // however the editor was started - and put in .editor/ beside the session
    // file, because a panel layout is exactly the same kind of thing: one
    // person's state, not the project's. See editor/src/EditorSession.h.
    //
    // THE STRING HAS TO OUTLIVE THIS FUNCTION. io.IniFilename is a const char*
    // that ImGui stores and reads for the rest of the program's life - it does
    // not copy it - so a local std::string here would leave ImGui reading freed
    // memory on the first frame. Hence the static.
    static std::string iniPath;
    iniPath         = eng::FileSystem::Resolve(".editor/imgui.ini");
    io.IniFilename  = iniPath.c_str();

    // A FIRST RUN AFTER THIS CHANGE WOULD OTHERWISE LOSE THE LAYOUT, because
    // the old file is still sitting where ImGui used to look. Moved once,
    // quietly, rather than making somebody rebuild an arrangement they had
    // already got right.
    AdoptLegacyIniIfPresent(iniPath);

    // DOCKING IS NOT ON BY DEFAULT. Without this line the panels float freely,
    // cannot be tabbed together, and no arrangement is remembered.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    auto* sdlWindow   = static_cast<SDL_Window*>(window.NativeWindowHandle());
    auto* sdlRenderer = static_cast<SDL_Renderer*>(window.NativeRendererHandle());

    // TWO backends have to be started. The first connects ImGui to SDL's input
    // and windowing; the second lets it draw through SDL's renderer. Starting
    // only the first gives you a program that runs perfectly and shows nothing.
    if (!ImGui_ImplSDL3_InitForSDLRenderer(sdlWindow, sdlRenderer)) {
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not connect ImGui to SDL");
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplSDLRenderer3_Init(sdlRenderer)) {
        ENGINE_LOG_ERROR(eng::Channels::kEditor,
                         "could not connect ImGui to the renderer");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    g_renderer    = sdlRenderer;
    g_initialised = true;

    // Hand the engine its three function pointers. From here on, every input
    // event goes past ImGui before it reaches the game.
    eng::GuiHooks hooks;
    hooks.ProcessEvent  = &HookProcessEvent;
    hooks.WantsKeyboard = &HookWantsKeyboard;
    hooks.WantsMouse    = &HookWantsMouse;
    eng::SetGuiHooks(hooks);

    ENGINE_LOG_INFO(eng::Channels::kEditor, "editor interface ready (ImGui {})",
                    IMGUI_VERSION);
    return true;
}

void EditorGui::Shutdown() {
    if (!g_initialised) {
        return;
    }

    // Clear the hooks FIRST. After this the engine stops calling in, so
    // nothing can reach ImGui while it is being taken apart.
    eng::SetGuiHooks(eng::GuiHooks{});

    // The exact reverse of Init: drawing backend, input backend, context.
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    g_renderer    = nullptr;
    g_initialised = false;
    ENGINE_LOG_INFO(eng::Channels::kEditor, "editor interface shut down");
}

bool EditorGui::IsInitialised() { return g_initialised; }

void EditorGui::BeginFrame() {
    if (!g_initialised) {
        return;
    }
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void EditorGui::EndFrame() {
    if (!g_initialised) {
        return;
    }
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), g_renderer);
}

void EditorGui::SetGameInputFocus(bool focused) {
    if (!g_initialised || focused == g_gameFocus) {
        return;
    }
    g_gameFocus = focused;

    // The second half of handing over the keyboard. With keyboard navigation
    // on, ImGui uses the arrow keys to move between widgets - so a Game view
    // that "had focus" still would not move the player with the arrow keys,
    // which looks exactly like broken input.
    ImGuiIO& io = ImGui::GetIO();
    if (focused) {
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    } else {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }
}

bool EditorGui::HasGameInputFocus() { return g_gameFocus; }

void EditorGui::SetGameViewArea(float x, float y, float width, float height,
                                bool live) {
    g_gameX    = x;
    g_gameY    = y;
    g_gameW    = width;
    g_gameH    = height;
    g_gameLive = live;
}

void EditorGui::ClearGameViewArea() {
    SetGameViewArea(0.0f, 0.0f, 0.0f, 0.0f, false);
}

void EditorGui::BeginDockspace() {
    if (!g_initialised) {
        return;
    }

    // A borderless window covering the whole screen whose only job is to hold
    // the docking area. The long list of flags is what stops it behaving like
    // an ordinary window - no title bar, cannot be moved, resized or closed.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar |
        // NoBackground matters more than it looks. The PassthruCentralNode
        // flag further down makes the DOCKING AREA see-through, but it does
        // nothing about this host WINDOW, which would otherwise paint a
        // 94%-opaque near-black rectangle over the entire screen. The symptom
        // is a dark sheet over the whole game that belongs to no panel and
        // cannot be closed - because it is not a panel, it is the thing the
        // panels are docked into. The two flags go together.
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##EngineDockspaceHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    const ImGuiID dockspaceId = ImGui::GetID("EngineDockspace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f),
                     ImGuiDockNodeFlags_PassthruCentralNode);

    // ---- the default arrangement -----------------------------------------
    //
    // Built ONCE, and only when the docking area is genuinely empty - which is
    // true the very first time the editor is run and false afterwards, because
    // ImGui saves any rearrangement into imgui.ini. That check is what makes
    // it safe to attempt this every run: a layout somebody set up by hand is
    // never overwritten.
    //
    // The arrangement is Unity's, because it is the one people already know:
    // Hierarchy on the left, Inspector on the right, Scene and Game tabbed
    // together in the middle, Assets and Console along the bottom.
    if (!g_layoutBuilt) {
        g_layoutBuilt = true;

        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
        const bool empty = (node == nullptr) || (node->IsEmpty() && !node->IsSplitNode());
        if (empty) {
            ImGui::DockBuilderRemoveNode(dockspaceId);

            // The two flags come from two different enum types inside ImGui -
            // one is part of its public interface and the other of its
            // internal one - and C++20 will not combine those with | directly.
            // Converting each to the plain integer type ImGui expects is the
            // fix, and it is what ImGui's own code does.
            const ImGuiDockNodeFlags nodeFlags =
                static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace) |
                static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_PassthruCentralNode);

            ImGui::DockBuilderAddNode(dockspaceId, nodeFlags);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

            // Each split carves a slice off the middle and hands back the id
            // of the new region; `centre` is repeatedly updated to whatever is
            // left over.
            ImGuiID       centre = dockspaceId;
            const ImGuiID left =
                ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.18f, nullptr, &centre);
            const ImGuiID right =
                ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.24f, nullptr, &centre);
            const ImGuiID top =
                ImGui::DockBuilderSplitNode(centre, ImGuiDir_Up, 0.08f, nullptr, &centre);
            const ImGuiID bottom =
                ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.30f, nullptr, &centre);

            ImGui::DockBuilderDockWindow("Toolbar", top);
            ImGui::DockBuilderDockWindow("Hierarchy", left);
            ImGui::DockBuilderDockWindow("Inspector", right);

            // Scene is docked FIRST so it is the tab showing on a first run -
            // the editor opens in edit mode, so the editing view belongs in
            // front.
            ImGui::DockBuilderDockWindow("Scene", centre);
            ImGui::DockBuilderDockWindow("Game", centre);

            // Assets first for the same reason: it is the panel you reach for
            // to start building something, and the Console is the one you go
            // looking for when something is wrong.
            ImGui::DockBuilderDockWindow("Assets", bottom);
            ImGui::DockBuilderDockWindow("Console", bottom);

            ImGui::DockBuilderFinish(dockspaceId);
            ENGINE_LOG_INFO(eng::Channels::kEditor,
                            "no saved window layout found, so the default one was built");
        }
    }
}

void EditorGui::EndDockspace() {
    if (!g_initialised) {
        return;
    }
    ImGui::End();
}

} // namespace editor
