# The scripting reference, and how to keep it honest

`docs/api/` is a browsable reference for every public type a game script can
reach — that is, everything behind `#include <engine/Engine.h>`. Open
`docs/api/index.html` in a browser; it needs no server.

Four files:

| | |
| --- | --- |
| `index.html` | the page, plus the hand-written guide pages as `<template>` blocks |
| `api.css` | the stylesheet |
| `api.js` | the renderer: routing, the class pages, the search |
| `api-data.js` | **generated.** Every type, every member, every example |

---

## The one promise this reference makes

**Every signature on every page is the declaration as it is written in its
header**, joined onto one line, not a paraphrase of it. Each page and each
member carries the header and line it came from.

That is only true for as long as nobody edits a header. These tools say when
it stops being true.

---

## Checking it

### Are the signatures still right?

```
perl tools/apidoc/verify.pl
```

Re-reads every documented declaration out of `engine/include/` and reports:

- **GONE** — documented, but no longer declared anywhere in its header. The
  page describes something that does not exist.
- **CHANGED** — still there, but the declaration reads differently now.
- **MOVED** — same declaration, different line. Harmless; the line numbers on
  the pages are just stale.

Exits non-zero on GONE or CHANGED, so it can gate a commit.

### Do the examples still compile?

Every example in the reference is real C++ and is checked by the real
compiler, at `/std:c++20` — the standard a project's scripts are built with,
not the engine's own C++23.

```
perl tools/apidoc/extract.pl
powershell -File tools/apidoc/compile-examples.ps1
perl tools/apidoc/analyse.pl
```

`extract.pl` writes each example out as its own translation unit under
`build/apidoc-examples/`. `compile-examples.ps1` builds a precompiled header
of `<engine/Engine.h>` and then syntax-checks all of them — about a minute for
the whole set; six seconds each without the pch. `analyse.pl` reads the log.

It needs a configured debug build tree, for nlohmann/json's headers. Pass
`-VcVars` if Visual Studio is somewhere other than the default.

**Reading the result.** The complete examples — whole script files, include to
registration macro — are held to **zero errors**, and `analyse.pl` exits
non-zero if any of them fails. Fragments are different: a snippet documenting
one member starts in the middle of a function and refers to a receiver it
never declared. The compiler says so and then cascades, so a unit with any
undeclared-identifier error is treated as assuming context rather than as a
defect. A fragment that fails *without* one is a real defect and is listed.

### Does an example call something that does not exist?

```
perl tools/apidoc/check-examples.pl
```

Cheaper than compiling and catches the thing that matters most: a
`Type::Member` that the reference itself does not document. Resolves the
`typedef`s at the bottom of `Engine.h` first, so `Input::IsPressed` is
recognised as `InputMap`.

---

## Regenerating it

`gen.pl` builds `api-data.js`. It is kept here for the signature extraction,
which is the part worth reading, and because `fixups.json` records the
corrections the compiler forced.

**It does not run from a clean checkout.** The descriptions, summaries and
examples were written by reading the headers, not parsed out of them, and
`gen.pl` reads that content back from a build journal that is not in this
repository. What it then does mechanically, and what `verify.pl` repeats, is:

- find each declaration in its header and **replace** the recorded signature
  with the file's own text
- correct line numbers, and drop any member whose name is nowhere in its header
- apply `fixups.json`, reporting any entry that no longer matches anything, so
  a stale correction cannot sit there unnoticed

To change wording on a page, edit `docs/api/api-data.js` directly and run
`verify.pl`. To add a type, add it there too — the renderer needs only
`name`, `kind`, `ns`, `header`, `line`, `audience`, `summary` and `members`.

---

## How the pages are organised

By module, with every type marked for who it is for:

- **Scripting** — a game script calls this directly.
- **Engine system** — runs itself; read the page for the frame order.
- **Host only** — the program that creates the window. A script never touches
  it, and it is in the reference because the umbrella header makes it visible.

Routing is on the hash: `#Entity`, `#Entity-Find`, `#guide-lifecycle`,
`#module-physics`, `#index`. Every type has a routing `slug` of letters,
digits and hyphens, separate from its display name — grouping pages are called
things like "Vec2 operators", which reads correctly as a title and is unusable
in a URL fragment.
