// music.cpp — QuickJS embedding entry point. See music.h.
//
// Step 1 scope: init runtime, expose print() to JS, run a smoke eval.
// Everything else (pattern engine, audio, scheduler) gets stacked on
// top of this module in subsequent steps.

#include "music.h"

extern "C" {
#include "quickjs.h"
}

#include <cstdio>
#include <cstring>

namespace {
    JSRuntime* g_rt  = nullptr;
    JSContext* g_ctx = nullptr;
    bool       g_up  = false;

    // Native implementation of `print(...)` — JS side calls print("hi").
    // Prefixed with [js] so console output is easy to filter.
    JSValue js_print(JSContext* ctx, JSValueConst /*this_val*/,
                     int argc, JSValueConst* argv) {
        std::fputs("[js] ", stdout);
        for (int i = 0; i < argc; i++) {
            if (i) std::fputc(' ', stdout);
            const char* s = JS_ToCString(ctx, argv[i]);
            if (!s) return JS_EXCEPTION;
            std::fputs(s, stdout);
            JS_FreeCString(ctx, s);
        }
        std::fputc('\n', stdout);
        return JS_UNDEFINED;
    }

    // Pretty-print a JS exception — name, message, stack — to stderr.
    void dump_exception(JSContext* ctx) {
        JSValue exc = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, exc);
        std::fprintf(stderr, "[js] exception: %s\n", msg ? msg : "<unprintable>");
        if (msg) JS_FreeCString(ctx, msg);

        JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
        if (!JS_IsUndefined(stack)) {
            const char* st = JS_ToCString(ctx, stack);
            if (st) {
                std::fprintf(stderr, "[js] stack:\n%s\n", st);
                JS_FreeCString(ctx, st);
            }
        }
        JS_FreeValue(ctx, stack);
        JS_FreeValue(ctx, exc);
    }
}

namespace Music {

bool init() {
    if (g_up) return true;

    g_rt = JS_NewRuntime();
    if (!g_rt) {
        std::fprintf(stderr, "[music] JS_NewRuntime failed\n");
        return false;
    }
    // 32 MB memory ceiling — plenty for pattern code; guards runaway
    // evaluations from trashing the host process.
    JS_SetMemoryLimit(g_rt, 32 * 1024 * 1024);
    // 1 MB stack ceiling — Strudel patterns recurse fairly deeply.
    JS_SetMaxStackSize(g_rt, 1 * 1024 * 1024);

    g_ctx = JS_NewContext(g_rt);
    if (!g_ctx) {
        std::fprintf(stderr, "[music] JS_NewContext failed\n");
        JS_FreeRuntime(g_rt); g_rt = nullptr;
        return false;
    }

    // Expose our print() helper as a global so smoke tests and Strudel's
    // console.log-style calls can land somewhere useful.
    JSValue global = JS_GetGlobalObject(g_ctx);
    JS_SetPropertyStr(g_ctx, global, "print",
                      JS_NewCFunction(g_ctx, js_print, "print", 1));
    // Minimal console.log shim — points at the same print function.
    // (Full console API gets added when we land @strudel/core.)
    JSValue console = JS_NewObject(g_ctx);
    JS_SetPropertyStr(g_ctx, console, "log",
                      JS_NewCFunction(g_ctx, js_print, "log", 1));
    JS_SetPropertyStr(g_ctx, global, "console", console);
    JS_FreeValue(g_ctx, global);

    g_up = true;
    std::fprintf(stdout, "[music] QuickJS %s runtime up\n", JS_GetVersion());
    return true;
}

void shutdown() {
    if (g_ctx) { JS_FreeContext(g_ctx); g_ctx = nullptr; }
    if (g_rt)  { JS_FreeRuntime(g_rt);  g_rt  = nullptr; }
    g_up = false;
}

bool eval(const std::string& code, const std::string& tag) {
    if (!g_up) return false;
    JSValue r = JS_Eval(g_ctx, code.c_str(), code.size(),
                        tag.c_str(), JS_EVAL_TYPE_GLOBAL);
    bool ok = !JS_IsException(r);
    if (!ok) dump_exception(g_ctx);
    JS_FreeValue(g_ctx, r);
    return ok;
}

} // namespace Music
