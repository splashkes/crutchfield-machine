# ADR-0010 — Embedded QuickJS as the music-engine runtime (no WebView)

**Status:** Accepted
**Date:** 2026-04-20

## Context

We wanted a standalone music system inside feedback.exe: paste a
Strudel-style pattern, hear it, have the visual loop influence the
music in real time. Three constraints were hard:

1. **No browser / Web APIs.** Simon's explicit rejection. The
   feedback system's value depends on tight real-time coupling
   between visuals and audio; a WebView inter-process hop kills that
   latency profile, and the "I hate it" feeling was unambiguous.
2. **Lightweight, embeddable, reliable.** Adding 150 MB of Chromium
   to a 5 MB feedback app was a non-starter.
3. **Strudel-compatible pattern syntax.** Users expect pasted
   snippets from strudel.cc to work. The pattern algebra (mini
   notation, `.fast`/`.every`/`.jux`, `stack`/`cat`) is the DSL —
   not the audio engine.

Options considered:

- **WebView2 / CEF** — rejected per constraint 1. WebView2 is
  invisible-browser-in-a-trenchcoat; also Windows-only, adds the
  runtime dependency.
- **Embed Node.js (libnode)** — 60+ MB static, doesn't fit constraint
  2. Also introduces an event-loop thread we'd need to coordinate
  with our render loop.
- **Reimplement pattern engine in native C++** — doable (~1500 lines)
  but gives up the Strudel-syntax compatibility. User snippets that
  use JS conditionals (`fb.zoom > 1 ? A : B`) or small helper
  functions become impossible.
- **QuickJS / QuickJS-ng** — chosen. ~500 KB static, pure C, ES2020
  support, MIT licensed, no JIT (good for sandbox), embeddable in
  any thread. Strudel's own pattern combinator code could notionally
  run here (though we implemented our own clean-room version — see
  ADR-0012).

## Decision

Vendor QuickJS-ng at `vendor/quickjs/`. Wrap it in `music.cpp` as a
single global JSRuntime + JSContext.

- **Initialization** — `Music::init()` creates runtime with 32 MB
  memory ceiling and 1 MB stack. Installs native `print()` +
  `console.log()` as global bindings for debug output. Loads
  `js/engine.js` once at startup; that file defines the `Pattern`
  class, mini-notation parser, and top-level constructors.
- **Evaluation** — pattern expressions are user-written JS that
  produces a Pattern (an object with `queryArc(begin, end)`). The
  C++ side evaluates `(USER_CODE).queryArc(a, b)` every lookahead
  cycle and marshals the array of haps back into C++ `Event`
  structs.
- **Video→JS bridge** — `Music::setScalar(name, value)` installs
  properties on the JS global `fb` object. Patterns read `fb.zoom`,
  `fb.theta`, etc. as plain numbers. Updated per frame from
  `main.cpp`.
- **Audio stays native.** QuickJS handles the pattern layer only.
  Emitted events get dispatched by `music.cpp`'s scheduler to the
  native sampler + synth + effects in `audio.cpp`. See the "Music
  + audio engine" section in ARCHITECTURE.md for the data flow.

## Consequences

**Positive:**
- Pattern code that's pasted from strudel.cc works for the subset
  we implement (mini notation, `stack`, `cat`, `.fast`, `.every`,
  etc.). See ADR-0012 for the clean-room scope.
- The `fb.X` globals let patterns respond to live feedback state
  with standard JS syntax (`.lpf(500 + fb.theta * 2000)`).
- Adding JS-side combinators is editing one text file
  (`js/engine.js`) with hot-reload behaviour (engine file is read
  at startup; restart app to pick up changes).
- Build footprint grew ~500 KB. Release zip went from 1.9 MB to
  2.4 MB including the JS engine + 5 music presets.
- No IPC, no network, no browser runtime dependency. Alt-tab,
  fullscreen state, and OS focus changes do not affect the music
  thread — only the render thread's dt pacing does.

**Negative:**
- QuickJS has no JIT, so heavy JS work (e.g. running Strudel's real
  transpiler on every evaluation) would be slower than V8. Not a
  practical issue for pattern code where the query runs ~4 times
  per second.
- We re-evaluate the pattern expression on every query slice
  (~250 ms) rather than caching a compiled Pattern object. That's
  the trade for supporting live modulation via `fb.X` values —
  caching would freeze the values read at construction time. If
  this becomes a perf issue, switch to explicit dynamic-value
  wrappers and cache the outer Pattern.
- Adding a combinator means editing JS, not C++. Contributors need
  to understand both sides. We mitigated by keeping `engine.js`
  small (~280 lines) and narrow.

## Alternatives rejected

- **Duktape / JerryScript / mujs** — smaller but ES5-era. Strudel
  and our engine both use modern syntax (classes, spread, arrow
  functions, template literals). Would have needed transpilation.
- **Custom scripting language / S-expressions** — throws away the
  "paste from strudel.cc" property that was a stated goal.
- **Audio via Web Audio stub in QuickJS** — we'd have to implement
  Web Audio nodes in C++ anyway; might as well skip the abstraction
  and write the DSP directly. Native DSP is what `audio.cpp` is.

## References

- QuickJS-ng: https://github.com/quickjs-ng/quickjs
- Earlier options discussion: commit `7f3d2d1` (step 1) through
  `4ac4cc6` (step 2) in the `music` branch.
- Related: ADR-0011 (virtual MIDI port), ADR-0012 (clean-room
  pattern engine), ADR-0013 (dt-coupled scheduler).
