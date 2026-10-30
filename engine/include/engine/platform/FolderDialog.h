#pragma once

// ============================================================================
//  FolderDialog.h - asking the operating system for a folder.
//
//  ==========================================================================
//  WHY THIS IS IN THE ENGINE AND NOT IN THE EDITOR
//
//  Three things now need somebody to pick a folder: exporting a game, opening
//  a project, and creating one. A launcher will want a fourth.
//
//  The picking itself is four lines. What is worth sharing is the CONTRACT
//  around it, which is easy to get subtly and invisibly wrong - see below -
//  and which was already written down once for the exporter. Copying it per
//  caller means copying a threading rule, and a threading rule that is right
//  in two places and wrong in the third fails on somebody else's machine
//  rather than on yours.
//
//  The engine is also the only layer allowed to include SDL. The editor gets
//  an exemption for ImGui's backend, but a launcher would not, so putting it
//  here is what lets anything else reuse it at all.
//
//  ==========================================================================
//  THE CONTRACT: THE ANSWER ARRIVES ON ANOTHER THREAD
//
//  Ask() RETURNS IMMEDIATELY. SDL's own documentation says the callback "may
//  be called from a different thread than the one the function was invoked
//  on", and on Windows it is. So the callback does exactly one thing - leave
//  a note under a lock - and the main thread collects it with Take() when it
//  is ready.
//
//  WHAT THE CALLBACK MUST NOT DO, and the reason is the same every time:
//
//    It must not log. Log and LogBuffer have no mutex in them anywhere, so
//    appending to that vector while the main thread draws the Console panel
//    from it is undefined behaviour - the sort that works perfectly until
//    somebody else runs it.
//
//    It must not touch the scene, the engine, or anything else the main
//    thread owns, for the same reason and more obviously.
//
//  Everything worth saying is said by Take()'s caller, on the main thread.
//
//  ==========================================================================
//  EVERY OUTCOME IS REPORTED, INCLUDING THE TWO THAT LOOK LIKE NOTHING
//
//  A dialog can be dismissed, and a dialog can fail to open at all. Both
//  arrive here as "no path", and collapsing them into one silent result is
//  how a menu item comes to do nothing for a reason nobody can find out.
//  Cancelled and Failed are told apart so the caller can say which.
// ============================================================================

#include <string>

namespace eng {

class FolderDialog {
public:
    enum class Choice {
        None,        // nobody has picked anything; the ordinary case every frame
        Chosen,      // a folder is waiting in `path`
        Cancelled,   // the dialog was dismissed
        Failed,      // the dialog itself could not run; `error` says why
    };

    struct Result {
        Choice      choice = Choice::None;
        std::string path;
        std::string error;

        // What the answer is FOR. Set by the caller when it asks, and handed
        // back with the answer.
        //
        // WHY IT IS HERE RATHER THAN IN THE CALLER: there is one dialog and
        // one pending answer, but several menu items that open it - New
        // Project, Open Project, Build Game. Without this, whoever collects
        // the answer first acts on it, and picking a folder for one thing
        // does the other.
        int purpose = 0;
    };

    // Opens the folder picker. Returns at once; see the header.
    //
    // `purpose` is handed back with the answer so the caller knows which of
    // its own questions was answered. Opening a second dialog while one is
    // already open replaces the first one's purpose, which is the honest
    // behaviour - there is only one answer to collect.
    static void Ask(int purpose = 0);

    // Main thread only: takes whatever the picker decided and clears it, so
    // one choice causes one action.
    static Result Take();
};

} // namespace eng
