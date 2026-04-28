// layers/vfx_slot.glsl
// Edirol V-4-inspired effect catalogue, per-slot dispatched. Two slots
// run back-to-back at the tail of the main pipeline; each holds one of
// 20 effects (plus "off") and exposes a single continuous CONTROL
// parameter that varies its behaviour across what the real V-4 spreads
// over 5–10 sub-variants.
//
// Effect IDs (match VFX_NAMES[] in main.cpp):
//    0 off          5 Colorize    10 Mirror-V    15 W-LumiKey
//    1 Strobe       6 Monochrome  11 Mirror-HV   16 B-LumiKey
//    2 Still        7 Posterize   12 Multi-H     17 ChromaKey
//    3 Shake        8 ColorPass   13 Multi-V     18 Fractal
//    4 Negative     9 Mirror-H    14 Multi-HV    19 VCR
//                                               20 Pixel
//
// Effect implementations land across commits C5a (color-family),
// C5b (geometric), C5c (temporal + key), plus live feedback-native additions.

// Slot-scoped uniforms driven by the host.
uniform int   uVfxEffect[2];
uniform float uVfxParam[2];
uniform int   uVfxBSource[2];

// BPM: provided so Strobe can beat-lock when the host asks for it.
uniform float uBpmPhase;       // 0..1 within the current beat
uniform int   uBpmStrobeLock;  // 1 = override param, use beat phase

// Rec. 709 luma — matches what the rest of the pipeline uses.
float vfx_lum(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Map a 0..1 parameter onto the full hue wheel, saturation = 1, value = 1.
vec3 vfx_hue_tint(float p) { return hsv2rgb(vec3(fract(p), 1.0, 1.0)); }

vec2 vfx_rot(vec2 q, float a) {
    float c = cos(a), s = sin(a);
    return mat2(c, -s, s, c) * q;
}

// Return the "B source" colour for key families.
//   bsrc == 0  → camera
//   bsrc == 1  → current feedback frame at the same uv (simple self-mix)
// For C5c we may post-process self to imitate V-4's second-bus feel.
vec4 vfx_b_source(vec2 uv, int bsrc) {
    if (bsrc == 0) return texture(uCam,  uv);
    /* bsrc == 1 */  return texture(uPrev, uv);
}

// Geometric VFX should alter where the feedback loop samples from, not replace
// the finished colour at the end of the shader. Keeping these in UV-space lets
// downstream layers (gamma/color/contrast/couple/noise/etc.) still act on them.
vec2 vfx_warp_uv(vec2 uv, int slot) {
    int   eff = uVfxEffect[slot];
    float p   = uVfxParam[slot];

    if (eff == 3) {                                // Shake
        float A = p * 0.05;
        float tA = uTime * 7.3;
        float tB = uTime * 11.7;
        vec2 j = vec2(hash21(vec2(floor(tA), 0.0), uTime) - 0.5,
                      hash21(vec2(0.0, floor(tB)), uTime) - 0.5) * 2.0 * A;
        return clamp(uv + j, 0.0, 1.0);
    }
    if (eff == 9) {                                // Mirror-H
        float s = clamp(p, 0.02, 0.98);
        if (uv.x > s) uv.x = 2.0 * s - uv.x;
        return clamp(uv, 0.0, 1.0);
    }
    if (eff == 10) {                               // Mirror-V
        float s = clamp(p, 0.02, 0.98);
        if (uv.y > s) uv.y = 2.0 * s - uv.y;
        return clamp(uv, 0.0, 1.0);
    }
    if (eff == 11) {                               // Mirror-HV
        float s = clamp(p, 0.02, 0.98);
        if (uv.x > s) uv.x = 2.0 * s - uv.x;
        if (uv.y > s) uv.y = 2.0 * s - uv.y;
        return clamp(uv, 0.0, 1.0);
    }
    if (eff == 12) {                               // Multi-H
        float N = floor(mix(2.0, 8.0, p) + 0.5);
        return vec2(fract(uv.x * N), uv.y);
    }
    if (eff == 13) {                               // Multi-V
        float N = floor(mix(2.0, 8.0, p) + 0.5);
        return vec2(uv.x, fract(uv.y * N));
    }
    if (eff == 14) {                               // Multi-HV
        float N = floor(mix(2.0, 8.0, p) + 0.5);
        return fract(uv * N);
    }
    if (eff == 18) {                               // Fractal
        vec2 q = uv - 0.5;
        float aspect = uRes.x / max(uRes.y, 1.0);
        q.x *= aspect;
        float zoom = mix(0.990, 0.955, p);
        float twist = mix(0.004, 0.045, p);
        q = vfx_rot(q * zoom, twist);
        q.x /= aspect;
        return clamp(q + 0.5, 0.0, 1.0);
    }
    if (eff == 19) {                               // VCR line displacement
        float line = floor(uv.y * uRes.y);
        float drift = hash21(vec2(line * 0.071, floor(uTime * 24.0)), uTime) - 0.5;
        float wobble = sin(uv.y * 80.0 + uTime * 5.0) * 0.0015;
        return clamp(uv + vec2((drift * 0.012 + wobble) * (0.35 + 0.65 * p), 0.0),
                     0.0, 1.0);
    }
    if (eff == 20) {                               // Pixel
        float block = floor(mix(4.0, 32.0, p) + 0.5);
        return (floor(uv * uRes / block) + 0.5) * block / uRes;
    }
    return uv;
}

// Apply the effect assigned to `slot` (0 or 1). `col` is the incoming
// colour, `uv` the current sample location. Returns the modified colour.
vec4 vfx_apply(vec4 col, vec2 uv, int slot) {
    int   eff   = uVfxEffect[slot];
    float p     = uVfxParam[slot];
    int   bsrc  = uVfxBSource[slot];

    // Effect dispatch. B-source is sampled inside each branch that needs it.
    if      (eff == 0)  return col;                // off
    else if (eff == 1) {                           // Strobe — intensity modulation
        // Two timing sources:
        //   bpm-locked: on the first half of each beat, off on the second.
        //   free:       period 2..30 frames, 50% duty, driven by uFrame.
        // Off-beats dim to 10% so trails aren't wiped from the field.
        float k;
        if (uBpmStrobeLock == 1) {
            k = (uBpmPhase < 0.5) ? 1.0 : 0.10;
        } else {
            int period = int(floor(mix(2.0, 30.0, p)));
            if (period < 2) period = 2;
            int phase  = int(uFrame) - (int(uFrame) / period) * period;
            k = (phase < period / 2) ? 1.0 : 0.10;
        }
        return vec4(col.rgb * k, col.a);
    }
    else if (eff == 2) {                           // Still — freeze feedback
        // param = mix between live col and held previous frame.
        vec4 held = texture(uPrev, uv);
        return vec4(mix(col.rgb, held.rgb, p), col.a);
    }
    else if (eff == 3) {                           // Shake
        return col;
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
        return col;
    }
    else if (eff == 10) {                          // Mirror-V (horizontal seam)
        return col;
    }
    else if (eff == 11) {                          // Mirror-HV (both)
        return col;
    }
    else if (eff == 12) {                          // Multi-H (horizontal tiling)
        return col;
    }
    else if (eff == 13) {                          // Multi-V
        return col;
    }
    else if (eff == 14) {                          // Multi-HV
        return col;
    }
    else if (eff == 15) {                          // W-LumiKey (bright = key)
        vec4 B = vfx_b_source(uv, bsrc);
        float L = vfx_lum(col.rgb);
        float edge = 0.06;
        float mask = smoothstep(p - edge, p + edge, L);
        return vec4(mix(col.rgb, B.rgb, mask), col.a);
    }
    else if (eff == 16) {                          // B-LumiKey (dark = key)
        vec4 B = vfx_b_source(uv, bsrc);
        float L = vfx_lum(col.rgb);
        float edge = 0.06;
        float mask = 1.0 - smoothstep(p - edge, p + edge, L);
        return vec4(mix(col.rgb, B.rgb, mask), col.a);
    }
    else if (eff == 17) {                          // ChromaKey (hue = key)
        vec4 B = vfx_b_source(uv, bsrc);
        vec3 hsv = rgb2hsv(col.rgb);
        float target = fract(p);
        float d = abs(fract(hsv.x - target + 0.5) - 0.5);  // 0..0.5 wrapped
        float tol = 0.10;
        float mask = 1.0 - smoothstep(tol, tol + 0.06, d);
        mask *= smoothstep(0.30, 0.45, hsv.y);             // ignore grays
        return vec4(mix(col.rgb, B.rgb, mask), col.a);
    }
    else if (eff == 18) {                          // Fractal
        vec2 q = uv - 0.5;
        float aspect = uRes.x / max(uRes.y, 1.0);
        q.x *= aspect;

        float angle = mix(0.09, 0.32, p);
        float scale = mix(0.78, 0.58, p);
        float gain = mix(0.18, 0.48, p);
        vec3 acc = vec3(0.0);
        float wsum = 0.0;
        float w = 0.50;
        for (int i = 0; i < 4; i++) {
            q = vfx_rot(q * scale, angle);
            vec2 tuv = q;
            tuv.x /= aspect;
            tuv += 0.5;
            if (tuv.x >= 0.0 && tuv.x <= 1.0 && tuv.y >= 0.0 && tuv.y <= 1.0) {
                vec3 s = texture(uPrev, tuv).rgb;
                acc += s * w;
                wsum += w;
            }
            w *= 0.58;
        }
        vec3 echo = acc / max(wsum, 0.001);
        vec3 rgb = max(col.rgb, echo * gain);
        rgb = mix(col.rgb, rgb, 0.45 + 0.35 * p);
        return vec4(clamp(rgb, 0.0, 1.0), col.a);
    }
    else if (eff == 19) {                          // VCR
        float line = floor(uv.y * uRes.y);
        vec3 rgb = col.rgb;
        rgb.r = mix(rgb.r, col.g, 0.12 * p);
        rgb.b = mix(rgb.b, col.g, 0.10 * p);
        float scan = 0.80 + 0.20 * sin(uv.y * uRes.y * 3.14159265);
        float tear = smoothstep(0.96, 1.0,
                                hash21(vec2(floor(uv.y * 80.0), floor(uTime * 8.0)), uTime));
        float grain = hash21(uv * uRes + vec2(float(uFrame), line), uTime) - 0.5;
        rgb = rgb * scan + vec3(grain * 0.08 * (0.5 + p)) + vec3(tear * 0.18);
        return vec4(clamp(rgb, 0.0, 1.0), col.a);
    }
    else if (eff == 20) {                          // Pixel
        vec3 rgb = col.rgb;
        float levels = floor(mix(10.0, 3.0, p) + 0.5);
        rgb = floor(rgb * levels + 0.5) / levels;
        return vec4(rgb, col.a);
    }

    return col;
}
