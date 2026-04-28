// layers/output_fade.glsl
// Bipolar output fade — the V-4's Output Fade dial as a single control.
//   uOutFade in [-1, +1]:
//      -1   → full black
//       0   → pass-through
//      +1   → full white
// Runs absolutely last, after the vfx slots. The faded colour is what
// gets written into the feedback field for next frame, so holding fade
// toward black is also how you wipe the scene to a clean state.
//
// Also hosts the loop's NaN/Inf sanitize — the one and only place in the
// default path where we touch individual float components for non-range
// reasons. Since the loop is deliberately unclamped (ADR-0015), divergent
// dynamics can produce Inf/NaN, and once a field holds NaN, decay (NaN*k
// = NaN) and inject (mix(NaN, p, 1) = NaN via NaN*0 = NaN) can no longer
// recover it. Sanitize at the final writeback so the feedback buffer
// never stores a poisoned pixel. Full range is still preserved; only
// non-finite components get clobbered. See ADR-0016.

uniform float uOutFade;

vec3 sanitize_finite(vec3 v) {
    // Ternary (not mix()) because mix(NaN, 0, 1) = NaN*0 + 0*1 = NaN.
    return vec3(isnan(v.r) || isinf(v.r) ? 0.0 : v.r,
                isnan(v.g) || isinf(v.g) ? 0.0 : v.g,
                isnan(v.b) || isinf(v.b) ? 0.0 : v.b);
}

vec4 output_fade_apply(vec4 col) {
    vec3 rgb = col.rgb;
    if (uOutFade != 0.0) {
        vec3 target = (uOutFade > 0.0) ? vec3(1.0) : vec3(0.0);
        rgb = mix(rgb, target, abs(uOutFade));
    }
    return vec4(sanitize_finite(rgb), col.a);
}
