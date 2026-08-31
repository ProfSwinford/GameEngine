#pragma once

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

#include <memory>

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
