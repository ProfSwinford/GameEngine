#pragma once

// ============================================================================
//  Platform.h - the operating-system window, the events it sends, and the SDL handles.
//
//  Each section below was its own file until the engine was reorganised; the
//  banner at the top of each one still explains that piece on its own.
// ============================================================================

#include <engine/modules/Subsystem.h>

#include <memory>
#include <string>
#include <vector>

// ============================================================================
//  SdlHandles.h - automatic clean-up for the objects SDL hands us.
//
//  WHAT SDL IS AND WHY THIS ENGINE USES IT
//  SDL3 (Simple DirectMedia Layer) is the library that actually opens a
//  window, reads the keyboard and mouse, and draws pixels. Doing those things
//  yourself means writing different code for Windows, macOS and Linux. SDL
//  does that part, so the rest of this engine can be one set of source files.
//
//  THE PROBLEM THIS FILE SOLVES
//  SDL is a C library, so everything it creates has to be destroyed by hand:
//
//      SDL_Window* w = SDL_CreateWindow(...);
//      ...
//      SDL_DestroyWindow(w);        // and if you forget, or if the code in
//                                   // between returns early, it leaks
//
//  C++ has a better answer. std::unique_ptr is the standard library's
//  "one owner" pointer: when it goes out of scope it destroys what it points
//  at, automatically, including on an early return. It normally calls
//  `delete`, which is wrong for an SDL object - so you can give it a custom
//  DELETER saying what to call instead. That is all the four structs below
//  are: a deleter each, and a friendlier name for the resulting pointer type.
//
//      WindowPtr window = ...;      // destroys itself; nothing to remember
//
//  WHY THE SDL TYPES ARE ONLY *DECLARED* HERE, NOT INCLUDED
//  The four lines like `struct SDL_Window;` tell the compiler "a type with
//  this name exists somewhere". That is enough to hold a pointer to one, and
//  it means no file that includes this header drags in the whole of SDL. The
//  actual `#include <SDL3/SDL.h>` happens in exactly one place, SdlHandles.cpp,
//  where the deleters are written. Keeping SDL out of the engine's public
//  headers is what allows the rest of the engine - and any game built on it -
//  to be written without knowing SDL exists.
// ============================================================================


// Forward declarations: names without definitions. See the note above.
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Surface;
struct SDL_Texture;

namespace eng {

// A deleter is just an object with an operator() that knows how to destroy one
// thing. These are declared here and written in SdlHandles.cpp, which is the
// only file that can see the real SDL functions.
struct SdlWindowDeleter   { void operator()(SDL_Window*   window)   const noexcept; };
struct SdlRendererDeleter { void operator()(SDL_Renderer* renderer) const noexcept; };
struct SdlSurfaceDeleter  { void operator()(SDL_Surface*  surface)  const noexcept; };
struct SdlTextureDeleter  { void operator()(SDL_Texture*  texture)  const noexcept; };

// Friendly names for "a unique_ptr that cleans up an SDL object".
using WindowPtr   = std::unique_ptr<SDL_Window,   SdlWindowDeleter>;
using RendererPtr = std::unique_ptr<SDL_Renderer, SdlRendererDeleter>;
using SurfacePtr  = std::unique_ptr<SDL_Surface,  SdlSurfaceDeleter>;
using TexturePtr  = std::unique_ptr<SDL_Texture,  SdlTextureDeleter>;

} // namespace eng


// ============================================================================
//  Window.h - the actual operating-system window the game appears in.
//
//  One of these exists for the whole program. It owns two things that SDL
//  gives us: the window itself, and the "renderer" attached to it (SDL's name
//  for the object that draws into a window).
//
//  NOTICE WHAT IS NOT IN THIS HEADER: the word SDL. Nothing outside the
//  platform layer needs to know which library opens the window, and keeping
//  that name out of the public headers is what would make swapping SDL for
//  something else an edit to a handful of .cpp files rather than to the whole
//  engine.
//
//  WHY COPYING A WINDOW IS FORBIDDEN
//  A Window owns a resource that belongs to the operating system. If you could
//  copy one, both copies would refer to the SAME OS window, and both would try
//  to close it when they were destroyed - closing something twice. There is no
//  sensible meaning for "a second copy of this window", so the class refuses
//  to compile the attempt (see the `= delete` lines below).
// ============================================================================



namespace eng {

class Window : public Subsystem {
public:
    // A window object starts out closed. Init is what opens it.
    Window() = default;

    // Opens a window of the size and title in the settings.
    //
    // If anything fails - no display attached, a driver problem - the object
    // is left INVALID rather than half-built, an explanation is written to the
    // log, and false comes back. No exception is thrown: a display that will
    // not open is a problem with the machine, not a bug in the code, and the
    // engine should be able to react to it and exit tidily.
    bool Init(const BootConfig& config) override;

    // Closes the renderer first and then the window, in that order. A window
    // destroyed out from under its own renderer is a crash. Doing this twice
    // is harmless.
    void Shutdown() override;

    // Closes the window if it is somehow still open. In the engine, the
    // ordered shutdown has already called Shutdown by the time this runs.
    ~Window() override;

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    bool IsValid() const;

    int  Width() const;
    int  Height() const;
    void SetTitle(const char* title);

    // Fills the whole window with one colour. Each channel is 0-255, which is
    // why they are `unsigned char` - that type holds exactly 0 to 255.
    void Clear(unsigned char r, unsigned char g, unsigned char b);

    // Shows the finished frame. Nothing drawn this frame is visible until this
    // is called.
    void Present();

    // ---------------------------------------------------------------------
    //  Engine-internal access to the underlying SDL objects.
    //
    //  Three parts of the engine legitimately need them: the editor GUI layer,
    //  the drawing layer, and the texture loader. All three live inside the
    //  engine.
    //
    //  They are returned as `void*` on purpose. Naming the SDL types here
    //  would put SDL back into the engine's public interface, which is exactly
    //  what this header is arranged to avoid. A `void*` says "this is plumbing,
    //  not part of the API" about as loudly as C++ can.
    //
    //  Game code never needs either of these.
    // ---------------------------------------------------------------------
    void* NativeWindowHandle() const;
    void* NativeRendererHandle() const;

private:
    // These two are declared in this order for a reason. C++ destroys members
    // in the REVERSE of their declaration order, so m_renderer (declared
    // second) is destroyed first - which is the order SDL requires.
    WindowPtr   m_window;
    RendererPtr m_renderer;

    bool        m_videoInitialised = false;
    std::string m_title            = "Engine2D";
};

} // namespace eng


// ============================================================================
//  EventPump.h - reads what the user did this frame.
//
//  The operating system collects everything the user does - key presses, mouse
//  movement, clicking the window's close button - into a queue. Once per frame
//  the EventPump empties that queue and stores what it found in a simple list
//  that the rest of the engine can look at.
//
//  WHY THIS LAYER EXISTS RATHER THAN CALLING SDL FROM THE GAME LOOP
//  Two reasons.
//
//  First, it keeps SDL in one place. Nothing in this header mentions SDL, so
//  game code, the editor, and the input system all read user input without
//  ever including an SDL header.
//
//  Second, what comes out of here is deliberately RAW and low-level: "scancode
//  44 went down". Game code should not be written against key numbers - see
//  input/InputMap.h, which turns these into named actions like "Jump" that the
//  player can rebind. This layer is the single doorway raw input comes through.
// ============================================================================


namespace eng {

// The kinds of thing that can happen. Deliberately coarse: InputMap turns
// these into named actions, and nothing above that layer sees a key number
// again.
enum class RawEventKind {
    None,
    Quit,              // the user asked to close the program
    KeyDown,
    KeyUp,
    MouseButtonDown,
    MouseButtonUp,
    MouseMove,
    MouseWheel,
    WindowResized,
    WindowFocusGained,   // the user switched back to this window
    WindowFocusLost,     // the user switched away from it
};

const char* ToString(RawEventKind kind);

// One thing that happened. Which fields are meaningful depends on `kind`:
// a KeyDown fills in `code`, a MouseMove fills in `mouseX`/`mouseY`, and so on.
struct RawEvent {
    int          code   = 0;      // which key, or which mouse button
    float        mouseX = 0.0f;   // in window pixels, measured from the top-left
    float        mouseY = 0.0f;
    float        wheelY = 0.0f;   // positive is scroll up
    RawEventKind kind   = RawEventKind::None;
};

class EventPump {
public:
    // Empties the operating system's queue and records everything in it.
    // Call this exactly once per frame.
    //
    // It keeps looping until the queue reports empty. Taking only one event
    // per call would make input lag by one frame for every event still waiting
    // - a bug that is very hard to recognise months later.
    //
    // The editor's GUI gets first look at each event. When a text box has
    // keyboard focus, the GUI claims the key presses; they are still recorded
    // here (so the list is complete) but marked as consumed, and the input
    // system skips those. Without that, typing a name into the Inspector would
    // also make the player jump.
    void Poll();

    // How many events arrived in the most recent Poll().
    std::size_t Count() const;

    // Event number `index` from the most recent Poll(). An index past the end
    // returns an inert "None" event and logs a warning rather than reading
    // memory that is not there.
    const RawEvent& At(std::size_t index) const;

    // True when the user asked to close the window this frame.
    bool QuitRequested() const;

    // True on the frame the window became the one being typed into - the user
    // alt-tabbed back, or clicked on it.
    //
    // The editor uses this to notice that scripts may have been edited while
    // it was in the background. A game could use it to pause itself when the
    // player switches away.
    bool FocusGainedThisFrame() const { return m_focusGained; }
    bool FocusLostThisFrame() const   { return m_focusLost; }

    // True when the editor GUI claimed event `index`. Gameplay input ignores
    // those.
    bool WasConsumed(std::size_t index) const;

    // Where the mouse is right now, in window pixels.
    //
    // Two separate floats rather than a Vec2 so that this header does not have
    // to depend on the maths layer for one field.
    float MouseX() const { return m_mouseX; }
    float MouseY() const { return m_mouseY; }

    // Turning key numbers into readable names and back. The config file stores
    // bindings as text like "Space", and these are what translate between that
    // and the numbers the operating system actually reports.
    static const char* KeyName(int code);
    static int         KeyCodeFromName(const char* name);
    static int         MouseButtonFromName(const char* name);

private:
    // A std::vector is used here and cleared (not destroyed) each frame.
    // clear() empties the list but keeps the memory it already had, so after
    // the first busy frame no further frame has to ask for more - which keeps
    // a per-frame function from allocating.
    std::vector<RawEvent> m_events;

    // Runs alongside m_events: one entry per event saying whether the GUI
    // claimed it.
    //
    // It stores `char` rather than `bool` on purpose. std::vector<bool> is a
    // special case in the standard library that packs its values into
    // individual bits, and as a result it does not behave like other vectors.
    // Avoiding it here avoids explaining that.
    std::vector<char> m_consumed;

    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;
    bool  m_quitRequested = false;
    bool  m_focusGained   = false;
    bool  m_focusLost     = false;
};

} // namespace eng


// ============================================================================
//  GuiHooks.h - the one place the engine lets a tool see input first.
//
//  WHY THIS EXISTS
//  When a text box in the editor has keyboard focus, that key press must NOT
//  also reach the game - otherwise typing an entity's name into the Inspector
//  makes the player jump. So something has to look at every event before the
//  input system does, and say "I claimed that one".
//
//  That something is Dear ImGui, and ImGui lives in the EDITOR, not in the
//  engine. The engine cannot call it directly without depending on the tool
//  that is built on top of it, which is backwards.
//
//  The answer is three function pointers. The engine calls them if they have
//  been set and carries on if they have not, so:
//
//    * the editor fills them in at start-up and gets first look at input
//    * the game leaves them empty and every event reaches it unfiltered
//
//  Nothing else in the engine knows the editor exists.
//
//  WHY PLAIN FUNCTION POINTERS RATHER THAN std::function
//  There is exactly one implementation, it is set once at start-up, and it is
//  called for every input event. A plain pointer is the simplest thing that
//  works and it makes "is this set?" a null check anyone can read.
// ============================================================================

namespace eng {

struct GuiHooks {
    // Offers one platform event to the tool layer. Returns true if it did
    // something with it. The pointer is an SDL_Event; it is void* here so this
    // header does not have to mention SDL.
    bool (*ProcessEvent)(const void* platformEvent) = nullptr;

    // True while the tool layer wants the keyboard - a text box has focus.
    bool (*WantsKeyboard)() = nullptr;

    // True while the tool layer wants the mouse - the cursor is over a panel.
    bool (*WantsMouse)() = nullptr;
};

// Called once by the editor at start-up. The game never calls it.
void SetGuiHooks(const GuiHooks& hooks);

// Used by EventPump. Returns hooks full of nullptr when nothing has been set.
const GuiHooks& GetGuiHooks();

} // namespace eng
