# ADR-0003 — Static-link everything MSYS2-provided for the Windows binary

**Status:** Accepted
**Date:** 2026-04-19

## Context

Pre-v0.1.0 the build produced a `feedback.exe` that dynamically linked
against five MSYS2-shipped DLLs: `glfw3.dll`, `glew32.dll`, `zlib1.dll`,
`libstdc++-6.dll`, `libwinpthread-1.dll`. These don't exist on a fresh
Windows install. A user downloading the exe from GitHub would see
"MSVCR_*.dll not found" errors (or equivalent) and bounce.

For a project aimed at non-developer audiences (installation artists,
researchers, video-art viewers), "install MSYS2 first" is a dealbreaker.

Options to fix the distribution:

1. **Ship the DLLs alongside the exe** in the zip. Works, but leaks
   implementation detail and bloats the archive with files the user
   doesn't care about.
2. **Install an MSVC redistributable** — requires admin, another
   install step.
3. **Static-link the MSYS2 libraries** into the exe. One-file
   distribution, zero runtime dependencies beyond Windows itself.

Static linking on MinGW has some well-known footguns:
- `-static-libstdc++` alone doesn't prevent libstdc++-6.dll dependency
  when the driver auto-appends `-lstdc++` after your LDLIBS under
  `-Wl,-Bdynamic`.
- `-pthread` in CXXFLAGS, if it reaches the link line, pulls dynamic
  libwinpthread.
- `-lglew32` resolves to `libglew32.dll.a` (the import lib) unless
  `GLEW_STATIC` is defined and `-Wl,-Bstatic` precedes it.

## Decision

Static-link `glfw3`, `glew32`, `zlib`, `winpthread`, and the C++ runtime
into `feedback.exe`. System DLLs (`opengl32`, `gdi32`, `mf*`, `winmm`,
`ole32`) remain dynamic — they're part of Windows.

Concretely in the Makefile:
- `-DGLEW_STATIC` in CXXFLAGS.
- Split CXXFLAGS (compile) from LDFLAGS (link) so `-pthread` doesn't
  expand to `-lpthread` on the link line.
- LDFLAGS: `-static -static-libgcc -static-libstdc++`.
- LDLIBS: `-Wl,-Bstatic -lglfw3 -lglew32 -lz -lwinpthread`, then
  `-Wl,-Bdynamic` before system libs, then a trailing `-Wl,-Bstatic`
  so the compiler's auto-appended `-lstdc++`/`-lgcc_s` also static-link.
- `make dist` target runs `objdump -p` as a sanity check to print the
  DLL import table.

## Consequences

**Positive:**
- Distribution is one `.exe` plus shaders/presets. Single zip, double-click to run.
- No "works on my machine" variance from DLL search paths.
- Binary is 3.5 MB — small for a GL app.
- `make dist` is the one command for packaging.

**Negative:**
- Can't benefit from system-wide library updates for the static libs.
- Binary is larger than a minimal dynamic build (~500KB vs ~3.5MB).
- The exact linker incantation is delicate. Changes to the `-Wl,-B*`
  blocks can silently reintroduce dynamic deps. The objdump check in
  `make dist` catches this.

## Alternatives considered

- **Ship DLLs next to the exe.** Rejected — every distribution
  platform (Steam, itch.io, random zip downloads) handles this
  differently; loose DLLs are fragile, get stripped by over-eager
  antivirus, and confuse users ("which file do I click?").
- **MSVC build as primary.** Would simplify static linking (MSVC
  bundles a static CRT) but requires users/contributors to have
  Visual Studio. MSYS2/MinGW is a smaller installation.
- **Use vcpkg's static triplet + MSVC.** Viable, but ties us to
  vcpkg's update cadence and makes `make` not the primary build.

## References

- `Makefile` — the static-link incantation, documented inline.
- `build_msvc.bat` — alternate MSVC path, less maintained.
- RUNBOOK.md — release procedure including the DLL sanity check.
