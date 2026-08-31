// WEEK 2 - the ImGui lifecycle. See EditorGui.h.
//
// This is the ONLY engine file that includes both SDL and ImGui headers.
// Keep it that way.
//
// Note that ImGui_ImplSDLRenderer3_RenderDrawData takes the renderer as a
// parameter. Tutorials that show it without predate a 2024 breaking change;
// when a tutorial and the header disagree, the header is right.

#include <engine/core/Log.h>
#include <engine/platform/Window.h>
#include <engine/tools/EditorGui.h>

#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder, for the default layout
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

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
        ENGINE_LOG_ERROR(Channels::kEditor, "EditorGui::Init called with an invalid window");
        return false;
    }

    IMGUI_CHECKVERSION();
    if (ImGui::CreateContext() == nullptr) {
        ENGINE_LOG_ERROR(Channels::kEditor, "ImGui::CreateContext failed");
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    // *** DOCKING IS NOT ON BY DEFAULT. *** Without this line the panels
    // float, cannot be tabbed, and no layout is saved - and the symptom looks
    // exactly like having cloned ImGui's master branch instead of the
    // v1.92.9b-docking tag.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    auto* sdlWindow   = static_cast<SDL_Window*>(window.NativeWindowHandle());
    auto* sdlRenderer = static_cast<SDL_Renderer*>(window.NativeRendererHandle());

    // Two backends. Forgetting the second is the classic failure: the app runs
    // perfectly and draws nothing at all.
    if (!ImGui_ImplSDL3_InitForSDLRenderer(sdlWindow, sdlRenderer)) {
        ENGINE_LOG_ERROR(Channels::kEditor, "ImGui_ImplSDL3_InitForSDLRenderer failed");
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplSDLRenderer3_Init(sdlRenderer)) {
        ENGINE_LOG_ERROR(Channels::kEditor, "ImGui_ImplSDLRenderer3_Init failed");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    g_renderer    = sdlRenderer;
    g_initialised = true;
    ENGINE_LOG_INFO(Channels::kEditor, "EditorGui up (ImGui {}, docking enabled)",
                    IMGUI_VERSION);
    return true;
}

void EditorGui::Shutdown() {
    if (!g_initialised) {
        return;
    }
    // Exact reverse of Init: renderer backend, platform backend, context.
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    g_renderer    = nullptr;
    g_initialised = false;
    ENGINE_LOG_INFO(Channels::kEditor, "EditorGui down");
}

bool EditorGui::IsInitialised() {
    return g_initialised;
}

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
    // While the Game view has focus the IDE gives up the keyboard entirely, so
    // EventPump stops marking key events consumed and InputMap sees them.
    if (g_gameFocus) {
        return false;
    }
    return ImGui::GetIO().WantCaptureKeyboard;
}

void EditorGui::SetGameInputFocus(bool focused) {
    if (!g_initialised || focused == g_gameFocus) {
        return;
    }
    g_gameFocus = focused;

    // The other half. With NavEnableKeyboard on, ImGui eats the arrow keys for
    // widget navigation - so a Game view that had "focus" would still not
    // move the player with the arrow keys, which looks exactly like broken
    // input rather than like a captured keyboard.
    ImGuiIO& io = ImGui::GetIO();
    if (focused) {
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    } else {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }
}

bool EditorGui::HasGameInputFocus() {
    return g_gameFocus;
}

bool EditorGui::WantsMouse() {
    return g_initialised && ImGui::GetIO().WantCaptureMouse;
}

void EditorGui::BeginDockspace() {
    if (!g_initialised) {
        return;
    }

    // A borderless, input-transparent host window covering the whole viewport,
    // whose only job is to own the dockspace. ImGui's own demo does exactly
    // this; the flags are what stop it behaving like a real window.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar |
                             // *** NoBackground IS LOAD-BEARING AND IT IS EASY TO MISS. ***
                             //
                             // PassthruCentralNode below makes the DOCK NODE
                             // transparent. It does nothing about this host
                             // WINDOW, which still paints ImGuiCol_WindowBg -
                             // and in StyleColorsDark that is
                             // (0.06, 0.06, 0.06, 0.94), a 94%-opaque near-black
                             // covering the entire viewport.
                             //
                             // The symptom is a semi-transparent dark sheet over
                             // the whole game that does not belong to any panel
                             // and cannot be closed, because it is not a panel -
                             // it is the thing the panels are docked into.
                             //
                             // The two flags have to be set together. ImGui's own
                             // demo couples them for exactly this reason.
                             ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##EngineDockspaceHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    const ImGuiID dockspaceId = ImGui::GetID("EngineDockspace");

    // PassthruCentralNode leaves the middle of the dockspace unoccupied.
    // Paired with NoBackground above - see the note there.
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f),
                     ImGuiDockNodeFlags_PassthruCentralNode);

    // ---- THE DEFAULT LAYOUT ------------------------------------------------
    //
    // Built ONCE, and only when the dockspace is genuinely empty - which is
    // true on a first run and false once imgui.ini exists, so a layout somebody
    // has arranged is never stomped. That check is the whole reason this is
    // safe to do unconditionally.
    //
    // The arrangement is Unity's, because it is the one people already know:
    // Hierarchy on the left, Inspector on the right, Scene and Game TABBED in
    // the centre, everything diagnostic along the bottom.
    if (!g_layoutBuilt) {
        g_layoutBuilt = true;

        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
        const bool empty = (node == nullptr) || (node->IsEmpty() && !node->IsSplitNode());
        if (empty) {
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace |
                                                       ImGuiDockNodeFlags_PassthruCentralNode);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

            ImGuiID centre = dockspaceId;
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

            // Scene FIRST, then Game, so Scene is the tab showing on a first
            // run - the editor opens in edit mode, so the editing view is the
            // one that should be in front.
            ImGui::DockBuilderDockWindow("Scene", centre);
            ImGui::DockBuilderDockWindow("Game", centre);

            // Assets FIRST in the bottom group, so it is the tab that is
            // showing when the editor opens - it is the panel you reach for
            // to start authoring, and the Log is the one you go looking for.
            ImGui::DockBuilderDockWindow("Assets", bottom);
            ImGui::DockBuilderDockWindow("Log", bottom);
            ImGui::DockBuilderDockWindow("Resources", bottom);
            ImGui::DockBuilderDockWindow("Profiler", bottom);
            ImGui::DockBuilderDockWindow("Memory", bottom);
            ImGui::DockBuilderDockWindow("CVars", bottom);
            ImGui::DockBuilderDockWindow("Jobs", bottom);
            ImGui::DockBuilderDockWindow("Event Inspector", bottom);
            ImGui::DockBuilderDockWindow("Viewport", right);
            ImGui::DockBuilderDockWindow("Debug Draw", right);

            ImGui::DockBuilderFinish(dockspaceId);
            ENGINE_LOG_INFO(Channels::kEditor,
                            "no saved layout found; built the default Unity-style "
                            "arrangement");
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
