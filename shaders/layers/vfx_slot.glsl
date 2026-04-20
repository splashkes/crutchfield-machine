// layers/vfx_slot.glsl
// Edirol V-4-inspired effect catalogue, per-slot dispatched. Two slots
// run back-to-back at the tail of the main pipeline; each holds one of
// 18 effects (plus "off") and exposes a single continuous CONTROL
// parameter that varies its behaviour across what the real V-4 spreads
// over 5–10 sub-variants.
//
// Effect IDs (match VFX_NAMES[] in main.cpp):
//    0 off          5 Colorize    10 Mirror-V    15 W-LumiKey
//    1 Strobe       6 Monochrome  11 Mirror-HV   16 B-LumiKey
//    2 Still        7 Posterize   12 Multi-H     17 ChromaKey
//    3 Shake        8 ColorPass   13 Multi-V     18 PinP
//    4 Negative     9 Mirror-H    14 Multi-HV
//
// Effect implementations land across commits C5a (color-family),
// C5b (geometric), C5c (temporal + key + PinP). This file currently
// scaffolds the dispatch + no-op bodies.

// Slot-scoped uniforms driven by the host.
uniform int   uVfxEffect[2];
uniform float uVfxParam[2];
uniform int   uVfxBSource[2];

// Return the "B source" colour for key/PinP families.
//   bsrc == 0  → camera
//   bsrc == 1  → current feedback frame at the same uv (simple self-mix)
// For C5c we may post-process self to imitate V-4's second-bus feel.
vec4 vfx_b_source(vec2 uv, int bsrc) {
    if (bsrc == 0) return texture(uCam,  uv);
    /* bsrc == 1 */  return texture(uPrev, uv);
}

// Apply the effect assigned to `slot` (0 or 1). `col` is the incoming
// colour, `uv` the current sample location. Returns the modified colour.
vec4 vfx_apply(vec4 col, vec2 uv, int slot) {
    int   eff   = uVfxEffect[slot];
    float p     = uVfxParam[slot];
    int   bsrc  = uVfxBSource[slot];

    // Common temporary so effects can sample a B source without repeat typing.
    vec4 B = (eff >= 15) ? vfx_b_source(uv, bsrc) : vec4(0);

    // 19-way dispatch. Each case is a stub returning `col` until its
    // implementation lands. Keeping the switch exhaustive so the shader
    // compiler catches missing cases if an effect id slips the table.
    if      (eff == 0)  return col;                // off
    else if (eff == 1)  return col;                // Strobe   (C5c)
    else if (eff == 2)  return col;                // Still    (C5c)
    else if (eff == 3)  return col;                // Shake    (C5b)
    else if (eff == 4)  return col;                // Negative (C5a)
    else if (eff == 5)  return col;                // Colorize (C5a)
    else if (eff == 6)  return col;                // Mono     (C5a)
    else if (eff == 7)  return col;                // Posterize(C5a)
    else if (eff == 8)  return col;                // ColorPass(C5a)
    else if (eff == 9)  return col;                // Mirror-H (C5b)
    else if (eff == 10) return col;                // Mirror-V (C5b)
    else if (eff == 11) return col;                // Mirror-HV(C5b)
    else if (eff == 12) return col;                // Multi-H  (C5b)
    else if (eff == 13) return col;                // Multi-V  (C5b)
    else if (eff == 14) return col;                // Multi-HV (C5b)
    else if (eff == 15) return col;                // W-LumiKey(C5c)  — B
    else if (eff == 16) return col;                // B-LumiKey(C5c)  — B
    else if (eff == 17) return col;                // ChromaKey(C5c)  — B
    else if (eff == 18) return col;                // PinP     (C5c)  — B

    // Silence unused warnings by reading each input (no-op in optimiser).
    return col + 0.0 * (B * p);
}
