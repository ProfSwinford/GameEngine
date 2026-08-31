# Week 1 — Notes

**Machine:** Windows 11 Pro 26200, MSVC 14.51 (Visual Studio 2026), CMake 4.3.1,
Ninja/MSBuild via the `debug` preset. SDL3 `release-3.4.14`, built from source
by FetchContent.

---

## The Ch. 2.2 exercise: declaration vs definition

`Window::Present` was commented out in `engine/src/platform/Window.cpp`, leaving
the declaration in `Window.h`. The build was run again. This is the error, pasted
verbatim:

```
engine.lib(Renderer.obj) : error LNK2019: unresolved external symbol
"public: void __cdecl eng::Window::Present(void)" (?Present@Window@eng@@QEAAXXZ)
referenced in function "public: static void __cdecl eng::Renderer::Present(void)"
(?Present@Renderer@eng@@SAXXZ)
  [C:\dev\Game Engine\GameEngineRepo\build\debug\sandbox\sandbox.vcxproj]

C:\dev\Game Engine\GameEngineRepo\build\debug\bin\Debug\sandbox.exe :
  fatal error LNK1120: 1 unresolved externals
```

**Which tool produced it.** `LNK2019` — the **linker**, not the compiler. Every
translation unit compiled without complaint, including `Renderer.cpp`, which is
the one that calls `Present()`.

**Why the compiler accepted a call to a function that did not exist.**

The compiler's job stops at the boundary of one translation unit. When it
compiled `Renderer.cpp` it had `Window.h` in front of it, and `Window.h`
*promises* that a function called `eng::Window::Present` taking no arguments and
returning `void` exists somewhere. That promise is all the compiler needs: it
knows the name, the parameter types and the return type, so it knows how to
build the call — where to put the arguments, what to expect back. It emits a
call to a symbol it has not seen defined and records "somebody please supply
`?Present@Window@eng@@QEAAXXZ`" in the object file. That is a complete,
correct object file.

The **linker** is the tool that collects every object file and matches each
outstanding request against the definitions the others supplied. It is the only
tool in the chain that can possibly notice the definition is missing, because it
is the only one that ever sees all the translation units at once.

Two things follow, and both are visible in the error text above:

- **There is no line number in a source file**, because there is no source line
  to name. The thing that is wrong is an *absence*. The best the linker can do
  is name the mangled symbol it wanted and the function that wanted it, which is
  exactly what it did.
- **The error names `Renderer.obj`, not `Window.cpp`.** The file that is
  *missing* something is not the file that gets blamed; the file that *asked* is.
  That is worth remembering, because the instinct on reading `LNK2019` is to go
  and look at the file named in the message, and the bug is never there.

Coming from C#, this stage does not exist in the same form: a C# assembly
carries its own metadata and the compiler resolves against referenced assemblies
directly, so "the method is declared but not implemented" is a compile error
with a line number. In C++ the header/implementation split buys separate
compilation — `Renderer.cpp` does not need to be recompiled when the body of
`Window::Present` changes — and the price is that this class of mistake is
caught later and reported with less information.

---

## A note on how this exercise was done

The first attempt did not produce an error at all. `Window::Present` had become
**dead code**: `Renderer::Present` was calling `SDL_RenderPresent` itself, so
nothing referenced the Window's version and the linker had no request to fail to
satisfy.

That was a real finding rather than a snag with the exercise — two functions in
the engine both presented the frame. `Renderer::Present` now delegates to
`Window::Present`, which removes the duplication, keeps the Week 1 API live, and
makes the exercise work.

---

## Two build-system traps found later, recorded here because they are Week 1's subject

Both were found in Week 10 and both are exactly the "works on my machine" class
this week is about, so they belong in this file rather than that one.

**1. `cmake --build --preset release` was silently building Debug.**

`CMAKE_BUILD_TYPE` is set in the *configure* preset. Multi-config generators —
Visual Studio, Xcode — **ignore it**, because they decide the configuration at
*build* time. So on Visual Studio the release preset configured with
`CMAKE_BUILD_TYPE=Release` and then built **Debug**, into `bin/Debug`.

Nothing errors. Nothing warns. The README says to run those two commands, and
the Week 4 and Week 5 labs both say "measure in a Release build" — so the
measurements would have been Debug numbers wearing a Release label, and the
AoS/SoA comparison would have been measuring the compiler's unoptimised output,
which is precisely what Week 4 says not to do.

The fix is one field per build preset: `"configuration": "Release"`. Single-config
generators (Ninja, Makefiles) ignore it, so it is correct everywhere.

*(The measurements in `docs/` are genuinely Release — they were taken with an
explicit `--config Release` before the preset was fixed. But they were right by
accident, which is not a state to leave a build system in.)*

**2. The generator is chosen by the environment, and one choice does not work
in a plain shell.**

The presets deliberately do not pin a generator, so the same file works on
Windows, Linux and macOS. The consequence is that CMake picks the platform
default — and on this machine that is **Visual Studio** inside a Developer
PowerShell and **Ninja** in a plain one.

Ninja invokes `cl.exe` directly and does not set up the MSVC environment the way
MSBuild does, so a plain shell produces:

```
fatal error C1083: Cannot open include file: 'cstddef': No such file or directory
```

That reads like a broken toolchain install and is nothing of the sort —
`INCLUDE` is simply unset. Same repo, same commands, same machine, different
shell. The README now says to build from a Developer PowerShell on Windows.

## `.gitignore` discipline

After a full build of the `debug`, `release` and `strict` presets,
`git status --porcelain` lists **no build artefact of any kind** — only the
source files themselves. `build/` is ignored, and so is `logs/` (the Week 3 file
sink writes there) and `imgui.ini` (Week 2 — a docked-panel layout is a personal
preference, not source). Both of those last two exist on disk after a run and
neither appears in `git status`, which is the check that matters.

Once the tree is committed, `git status --porcelain` after a full build prints
nothing at all; `scripts/fresh-clone-check.sh` asserts exactly that inside a
throwaway clone and fails if it prints anything.

`assets/**` is explicitly *un*-ignored at the bottom of `.gitignore`. That is
deliberate and it is the Week 9 milestone's failure mode arriving early: a scene
that loads on this machine and not on a fresh clone almost always means a `.bmp`
was never committed.

---

## The fresh-clone drill

`scripts/fresh-clone-check.sh` automates it (Week 1 stretch goal 2, finished).
It clones into a throwaway directory, configures, builds, checks that the
executables exist at the expected path, and then runs `git status --porcelain`
inside the clone and fails if it prints anything — which is the part that proves
`.gitignore` is complete rather than merely plausible.

---

## Stretch goals

**1. Warning-clean in both configurations.** Both `debug` and `release` build
with zero warnings at `/W4 /permissive-`. The `strict` preset adds `/WX`.

**2. `scripts/fresh-clone-check.sh` finished**, including the
`git status --porcelain` check.

**3. Command-line window size — deliberately NOT kept.** It was written in
Week 1 and deleted in Week 8, exactly as the lab predicted. Week 8's config file
solves the same problem properly: `config/engine.json` sets the window size, and
changing it takes effect on the next launch with no rebuild. The flags that
remain on `sandbox` (`--layout-bench`, `--os-measure`, `--random-check`,
`--stress`, `--sizeof-audit`, `--fail-subsystem`) select which of the semester's
*measurements* to run, which is a different job from configuring the engine.
