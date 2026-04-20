// music.h — embedded JavaScript runtime for pattern-driven audio.
//
// Step 1: just stand up QuickJS, register a print() function, run a
// "hello from JS" snippet at startup. Future steps add Strudel's pattern
// packages, a native sampler/synth, and a bridge exposing video-side
// scalars back into JS so patterns can react to what's on screen.
//
// We keep a single global Music instance; init() must be called once
// before any eval(). The audio thread, scheduler, and DSP all live
// behind this same abstraction so callers don't care about QuickJS.

#pragma once

#include <string>

namespace Music {

// Boot the JS runtime. Idempotent — second calls are no-ops. Returns
// false on failure (prints to stderr).
bool init();

// Shut down the runtime and free everything. Safe to call even if
// init() failed or was never called.
void shutdown();

// Evaluate a chunk of JS source. `tag` is used in error messages to
// identify the source (e.g. a filename or "<startup>"). Returns true
// on success; prints any exception to stderr.
bool eval(const std::string& code, const std::string& tag);

} // namespace Music
