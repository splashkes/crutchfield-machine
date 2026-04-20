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

// Rec. 709 luma — matches what the rest of the pipeline uses.
float vfx_lum(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Map a 0..1 parameter onto the full hue wheel, saturation = 1, value = 1.
vec3 vfx_hue_tint(float p) { return hsv2rgb(vec3(fract(p), 1.0, 1.0)); }

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
    else if (eff == 3) {                           // Shake
        // Position jitter. param = amplitude in the 0..5% of frame range.
        // Two-frequency random drift via hash on uTime gives less regular
        // motion than a pure sine would.
        float A = p * 0.05;
        float tA = uTime * 7.3;
        float tB = uTime * 11.7;
        vec2 j = vec2(hash21(vec2(floor(tA), 0.0), uTime) - 0.5,
                      hash21(vec2(0.0, floor(tB)), uTime) - 0.5) * 2.0 * A;
        return texture(uPrev, uv + j);
    }
    else if (eff == 4) {                           // Negative
        // param = mix between original and full RGB inversion.
        vec3 inv = 1.0 - col.rgb;
        return vec4(mix(col.rgb, inv, p), col.a);
    }
    else if (eff == 5) {                           // Colorize
        // param = hue of tint. Luminance preserved; mix 60% toward tinted
        // luminance so colour remains bent rather than pure-paletted.
        vec3 tint = vfx_hue_tint(p);
        float L   = vfx_lum(col.rgb);
        vec3 tinted = vec3(L) * tint * 1.5;          // 1.5 compensates the darkening from mono conversion
        return vec4(mix(col.rgb, tinted, 0.75), col.a);
    }
    else if (eff == 6) {                           // Monochrome
        // Pure luma × tint. param = tint hue. Classic B&W with a cast.
        vec3 tint = vfx_hue_tint(p);
        float L   = vfx_lum(col.rgb);
        return vec4(vec3(L) * tint, col.a);
    }
    else if (eff == 7) {                           // Posterize
        // Reduce brightness gradations. param sweeps level count 2..16
        // (exponential-ish for a smoother sweep at low levels).
        float levels = floor(mix(2.0, 16.0, p * p));
        vec3 q = floor(col.rgb * levels + 0.5) / levels;
        return vec4(q, col.a);
    }
    else if (eff == 8) {                           // ColorPass
        // Preserve colours whose hue is near `p`, desaturate everything else.
        // Tolerance widens with lower param — so p near 0 or 1 is narrow,
        // p near 0.5 catches a broad hue band? No — we fix tolerance and
        // let param drive the target hue.
        vec3 hsv = rgb2hsv(col.rgb);
        float target = fract(p);                     // target hue
        float d = abs(fract(hsv.x - target + 0.5) - 0.5); // 0..0.5 wrapped
        float tol = 0.08;                            // ±8% of hue wheel
        float keep = 1.0 - smoothstep(tol, tol + 0.06, d);
        float L = vfx_lum(col.rgb);
        vec3 mono = vec3(L);
        return vec4(mix(mono, col.rgb, keep), col.a);
    }
    else if (eff == 9) {                           // Mirror-H (vertical seam)
        // Reflect the half on the far side of the seam onto the near side.
        // param slides the seam 0..1; 0.5 = classic centre mirror.
        float s = clamp(p, 0.02, 0.98);
        vec2 mu = uv;
        if (uv.x > s) mu.x = 2.0 * s - uv.x;
        return texture(uPrev, clamp(mu, 0.0, 1.0));
    }
    else if (eff == 10) {                          // Mirror-V (horizontal seam)
        float s = clamp(p, 0.02, 0.98);
        vec2 mu = uv;
        if (uv.y > s) mu.y = 2.0 * s - uv.y;
        return texture(uPrev, clamp(mu, 0.0, 1.0));
    }
    else if (eff == 11) {                          // Mirror-HV (both)
        float s = clamp(p, 0.02, 0.98);
        vec2 mu = uv;
        if (uv.x > s) mu.x = 2.0 * s - uv.x;
        if (uv.y > s) mu.y = 2.0 * s - uv.y;
        return texture(uPrev, clamp(mu, 0.0, 1.0));
    }
    else if (eff == 12) {                          // Multi-H (horizontal tiling)
        // Integer tile count 2..8 via continuous param.
        float N = floor(mix(2.0, 8.0, p) + 0.5);
        vec2 tu = vec2(fract(uv.x * N), uv.y);
        return texture(uPrev, tu);
    }
    else if (eff == 13) {                          // Multi-V
        float N = floor(mix(2.0, 8.0, p) + 0.5);
        vec2 tu = vec2(uv.x, fract(uv.y * N));
        return texture(uPrev, tu);
    }
    else if (eff == 14) {                          // Multi-HV
        float N = floor(mix(2.0, 8.0, p) + 0.5);
        vec2 tu = fract(uv * N);
        return texture(uPrev, tu);
    }
    else if (eff == 15) return col;                // W-LumiKey(C5c)  — B
    else if (eff == 16) return col;                // B-LumiKey(C5c)  — B
    else if (eff == 17) return col;                // ChromaKey(C5c)  — B
    else if (eff == 18) return col;                // PinP     (C5c)  — B

    // Silence unused warnings by reading each input (no-op in optimiser).
    return col + 0.0 * (B * p);
}
