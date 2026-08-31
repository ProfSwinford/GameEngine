#pragma once

// =============================================================================
//  WEEK 3 - the week the raw SDL pointers died.
//
//  Coming from C#: you are used to `using (var x = ...)`, IDisposable, and a
//  garbage collector that eventually cleans up whatever you forgot. C++ has
//  neither, and instead has something arguably better - an object's destructor
//  runs DETERMINISTICALLY when it goes out of scope. Not eventually. Not at a
//  GC pause. At the closing brace.
//
//  RAII exploits that: acquire in a constructor, release in a destructor, and
//  forgetting becomes impossible, because leaving the scope is not optional.
//
//  ---------------------------------------------------------------------------
//  WHY unique_ptr AND NOT shared_ptr: there is exactly one owner of a window.
//  Shared ownership of a window is not a thing you want to be able to express,
//  and a type that cannot express it is a type that cannot get it wrong.
//
//  ---------------------------------------------------------------------------
//  THE INCOMPLETE-TYPE RITE OF PASSAGE, and what it actually was.
//
//  The forward declarations below let unique_ptr HOLD a pointer to a type it
//  has never seen. What it cannot do is DESTROY one: ~unique_ptr calls the
//  deleter, and the deleter has to see the complete type - or, in this design,
//  has to be a function whose definition lives somewhere that does.
//
//  The wall of "invalid application of sizeof to an incomplete type" errors
//  appears at the point the destructor is INSTANTIATED, which for a member of
//  eng::Window is inside Window's own destructor - so the error names a file
//  that does not mention SDL at all, which is what makes it confusing.
//
//  The fix used here: operator() is DECLARED in this header and DEFINED in
//  SdlHandles.cpp, which includes <SDL3/SDL.h>. Nothing else in the engine
//  needs the complete type, and no header outside the platform layer contains
//  the text SDL_Window or SDL_Renderer.
//
//  TARGET FOR THE WEEK, MET: grepping engine/include and sandbox/ for
//  SDL_Window* or SDL_Renderer* finds nothing outside this file.
//  Week 1's Window::RawRenderer() escape hatch was deleted with it.
// =============================================================================

#include <memory>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Surface;
struct SDL_Texture;

namespace eng {

// A deleter is a callable that knows how to destroy one thing. Declared here,
// defined in the .cpp where SDL is complete. Stateless, so unique_ptr stays
// the size of a bare pointer - `sizeof(WindowPtr) == sizeof(void*)`, which is
// one of the numbers in the Week 4 sizeof audit.
struct SdlWindowDeleter   { void operator()(SDL_Window*   window)   const noexcept; };
struct SdlRendererDeleter { void operator()(SDL_Renderer* renderer) const noexcept; };
struct SdlSurfaceDeleter  { void operator()(SDL_Surface*  surface)  const noexcept; };
struct SdlTextureDeleter  { void operator()(SDL_Texture*  texture)  const noexcept; };

using WindowPtr   = std::unique_ptr<SDL_Window,   SdlWindowDeleter>;
using RendererPtr = std::unique_ptr<SDL_Renderer, SdlRendererDeleter>;
using SurfacePtr  = std::unique_ptr<SDL_Surface,  SdlSurfaceDeleter>;
using TexturePtr  = std::unique_ptr<SDL_Texture,  SdlTextureDeleter>;

} // namespace eng
