#pragma once

// =============================================================================
//  WEEK 1 - the window. WEEK 3 - the RAII conversion.
//
//  Week 1 held raw SDL_Window* and SDL_Renderer* members and exposed
//  RawRenderer() as an escape hatch so the sandbox could poll events. Week 2
//  replaced that use with EventPump; Week 3 converted the members to
//  unique_ptr with custom deleters and DELETED RawRenderer() - the hatch had
//  expired.
//
//  What the header contains now: no SDL type name, anywhere. That is what
//  allowed engine/CMakeLists.txt to change SDL from PUBLIC to PRIVATE, which
//  is the actual architectural achievement of Milestone 0.
//
//  ---------------------------------------------------------------------------
//  WHY THE COPY OPERATIONS ARE DELETED (the Week 1 question, answered):
//
//  A Window owns an OS resource. The default copy constructor copies members
//  bitwise, so two Windows would hold the same SDL handles, and BOTH
//  destructors would destroy them - a double free, plus a window that vanishes
//  when a temporary copy goes out of scope. There is no sensible meaning for
//  "a second copy of this window", so the type refuses to express it.
//
//  This is the first place C++ value semantics bite someone coming from C#,
//  where copying a class variable copies a reference and the GC sorts out the
//  rest. In C++ the compiler will happily write a copy constructor that is
//  catastrophically wrong unless you tell it not to. `= delete` turns a
//  runtime crash into a compile error.
//
//  (The same reasoning is why unique_ptr itself is move-only, which is a nice
//  demonstration that the ownership model composes: because the members are
//  non-copyable, this class would be non-copyable even without the = delete.
//  The explicit deletion is kept because saying it out loud is documentation.)
// =============================================================================

#include <engine/core/Types.h>
#include <engine/platform/SdlHandles.h>

#include <string>

namespace eng {

class Window {
public:
    // Creates a window of the given size and title, plus a renderer for it.
    // EVERY SDL call's return value is checked; on any failure the object is
    // left INVALID rather than half-constructed, an explanation is logged, and
    // IsValid() returns false. No exception is thrown: a display that will not
    // open is an environment failure (Ch. 3.2), so the caller gets an error to
    // respond to.
    Window(const char* title, i32 width, i32 height);

    // Tears down in the exact reverse of construction: renderer, then window,
    // then the SDL video subsystem. The member declaration ORDER is what
    // enforces the first two - members are destroyed in reverse declaration
    // order, so m_renderer is declared after m_window and therefore dies
    // first. Getting that backwards destroys a window out from under its own
    // renderer, which SDL does not enjoy.
    //
    // Survives a construction that failed partway: unique_ptr members are null
    // and SDL's destroy functions accept null.
    ~Window();

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    bool IsValid() const;

    i32  Width() const;
    i32  Height() const;
    void SetTitle(const char* title);

    // Fills the whole window with one colour.
    void Clear(u8 r, u8 g, u8 b);

    // Pushes the finished frame to the screen.
    void Present();

    // ---------------------------------------------------------------------
    //  ENGINE-INTERNAL native handles.
    //
    //  Three engine-side consumers legitimately need the platform objects:
    //  the ImGui backend (tools/EditorGui), the draw layer (platform/Renderer)
    //  and the texture loader (resource/ResourceManager). All three live
    //  inside the engine.
    //
    //  They are typed `void*` deliberately. The alternative - naming the SDL
    //  types here, even as forward declarations - would put SDL back in the
    //  public interface and undo the PUBLIC->PRIVATE change. void* says "this
    //  is not part of the game-facing API" as loudly as the type system can.
    //
    //  Game code has no use for these and the gate exercise never touched them.
    // ---------------------------------------------------------------------
    void* NativeWindowHandle() const;
    void* NativeRendererHandle() const;

private:
    // Declaration order IS destruction order, reversed. See ~Window.
    WindowPtr   m_window;
    RendererPtr m_renderer;

    bool        m_videoInitialised = false;
    std::string m_title;
};

} // namespace eng
