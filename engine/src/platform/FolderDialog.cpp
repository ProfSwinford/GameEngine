// ============================================================================
//  FolderDialog.cpp - see FolderDialog.h for the threading contract, which is
//  the only interesting thing about this file.
// ============================================================================

#include <engine/platform/FolderDialog.h>

#include <engine/Engine.h>

#include <SDL3/SDL.h>

#include <mutex>

namespace eng {
namespace {

// Where the picker's answer waits until the main thread collects it.
//
// A MUTEX RATHER THAN A BARE STRING. SDL calls the callback on its own thread
// on Windows, and two threads touching one std::string with no lock is
// undefined behaviour - the kind that works every time you test it and fails
// on somebody else's machine.
std::mutex            g_mutex;
FolderDialog::Result  g_pending;

// What the dialog currently open was asked for. Read by the callback, so it
// is written before the dialog is opened and guarded by the same lock.
int g_purpose = 0;

// THIS RUNS ON SDL'S DIALOG THREAD, NOT THE MAIN ONE. It leaves a note under
// a lock and does nothing else - no logging, no engine, no scene. See the
// header for why each of those would be undefined behaviour rather than
// untidiness.
void OnChosen(void* /*userdata*/, const char* const* filelist, int /*filter*/) {
    FolderDialog::Result result;

    if (filelist == nullptr) {
        // The dialog itself could not run. SDL_GetError is safe here; it is
        // per-thread.
        result.choice      = FolderDialog::Choice::Failed;
        const char* reason = SDL_GetError();
        result.error       = (reason != nullptr && reason[0] != '\0')
                                 ? reason
                                 : "the operating system did not say why";
    } else if (filelist[0] == nullptr) {
        result.choice = FolderDialog::Choice::Cancelled;
    } else {
        result.choice = FolderDialog::Choice::Chosen;
        result.path   = filelist[0];
    }

    const std::lock_guard<std::mutex> lock(g_mutex);
    result.purpose = g_purpose;
    g_pending      = result;
}

} // namespace

void FolderDialog::Ask(int purpose) {
    {
        const std::lock_guard<std::mutex> lock(g_mutex);
        g_purpose = purpose;
    }

    // THE WINDOW IS PASSED AS THE PARENT, so the dialog is modal to it and
    // cannot end up behind it - a folder picker lost behind a maximised
    // editor is indistinguishable from a menu item that does nothing.
    //
    // Window::NativeWindowHandle hands back the SDL_Window as a void*, which
    // is how the engine exposes its platform objects everywhere: the type is
    // never named in a public header, and the one program that already links
    // SDL casts it back. See Window.h.
    auto* window =
        static_cast<SDL_Window*>(Engine::Get().GetWindow().NativeWindowHandle());

    SDL_ShowOpenFolderDialog(OnChosen, nullptr, window, nullptr, false);
}

FolderDialog::Result FolderDialog::Take() {
    const std::lock_guard<std::mutex> lock(g_mutex);
    const Result                      taken = g_pending;
    g_pending                               = Result{};
    return taken;
}

} // namespace eng
