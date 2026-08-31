// ============================================================================
//  EditorGui.cpp - starting, running and stopping Dear ImGui. See EditorGui.h.
//
//  This is the only file in the engine that includes both SDL and ImGui.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/platform/Window.h>
#include <engine/tools/EditorGui.h>

#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_internal.h>          // DockBuilder, used for the default layout
#include <imgui_impl_sdl3.h>         // the input half of ImGui's SDL support
#include <imgui_impl_sdlrenderer3.h> // the drawing half

namespace eng {
namespace {

bool          g_initialised = false;
SDL_Renderer* g_renderer    = nullptr;
bool          g_gameFocus   = false;
bool          g_layoutBuilt = false;

} // namespace

bool EditorGui::Init(Window& window) {
    if (g_initialised) {
        return true;
    }
    if (!window.IsValid()) {
        ENGINE_LOG_ERROR(Channels::kEditor,
                         "the editor interface was given a window that failed to open");
        return false;
    }

    IMGUI_CHECKVERSION();
    if (ImGui::CreateContext() == nullptr) {
        ENGINE_LOG_ERROR(Channels::kEditor, "could not create the ImGui context");
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();

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
        ENGINE_LOG_ERROR(Channels::kEditor, "could not connect ImGui to SDL");
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplSDLRenderer3_Init(sdlRenderer)) {
        ENGINE_LOG_ERROR(Channels::kEditor, "could not connect ImGui to the renderer");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    g_renderer    = sdlRenderer;
    g_initialised = true;
    ENGINE_LOG_INFO(Channels::kEditor, "editor interface ready (ImGui {})", IMGUI_VERSION);
    return true;
}

void EditorGui::Shutdown() {
    if (!g_initialised) {
        return;
    }
    // The exact reverse of Init: drawing backend, input backend, context.
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    g_renderer    = nullptr;
    g_initialised = false;
    ENGINE_LOG_INFO(Channels::kEditor, "editor interface shut down");
}

bool EditorGui::IsInitialised() { return g_initialised; }

bool EditorGui::ProcessEvent(const void* sdlEvent) {
    if (!g_initialised || sdlEvent == nullptr) {
        return false;
    }
    return ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(sdlEvent));
}

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

bool EditorGui::WantsKeyboard() {
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

bool EditorGui::WantsMouse() {
    return g_initialised && ImGui::GetIO().WantCaptureMouse;
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

        ImGuiDockNode* node  = ImGui::DockBuilderGetNode(dockspaceId);
        const bool     empty = (node == nullptr) || (node->IsEmpty() && !node->IsSplitNode());
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
            ENGINE_LOG_INFO(Channels::kEditor,
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

} // namespace eng
