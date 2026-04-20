// layers/physics.glsl
// Crutchfield-faithful camera-side physics. Four knobs from the 1984 paper
// that we previously omitted, gathered behind one toggle (L_PHYSICS, F2-key
// shifted... actually a new F-key — see main.cpp).
//
//   uInvert        : 0 = off, 1 = on (Crutchfield's "s" parameter; flips
//                    every channel as col = 1 - col before processing).
//                    Section 4 / Plates 6-7: produces pinwheels and
//                    Belousov-Zhabotinsky-like color waves.
//   uSensorGamma   : photoconductor response gamma (Crutchfield Appendix:
//                    i₀ ∝ Iᵢ^γ, γ ∈ [0.6, 0.9]). Applied at sample time —
//                    distinct from display gamma (gamma.glsl) which sits
//                    around the loop's "analog" stages.
//   uSatKnee       : soft saturation knee strength (0 = hard clip; 1 = full
//                    Reinhard-style x/(x+1) rolloff). Models the camera's
//                    saturation above I_sat threshold.
//   uColorCross    : RGB cross-coupling (0 = R/G/B independent; 1 = full
//                    averaging per-pass). Crutchfield eq. (5): off-diagonal
//                    elements of the L̄ and L̄' matrices, modeling monitor
//                    misconvergence, optical aberration, electronic crosstalk.
//
// All four default to "off"-equivalent values (1.0, 1.0, 0.0, 0.0) so the
// layer can be enabled with no immediate visual change, then dialed in
// individually.
//
// Deliberately NOT clamped to [0,1]. The loop runs in float precisely so
// values can exceed the display range and come back via decay / contrast
// shaping. Camera-side saturation is modeled by the `uSatKnee` Reinhard
// rolloff, not by a hard clip. See development/LAYERS.md §float-precision
// invariant and ADR-0015.

vec4 physics_apply(vec4 c) {
    vec3 rgb = c.rgb;
    // Invert used to live at the top of this function, but that meant
    // enabling "V" (invert) silently did nothing whenever the Physics
    // layer was off — a very confusing UX. It now runs unconditionally
    // in main.frag (see the invert stage near the start of the pipeline).

    // 2. Sensor gamma (input curve, distinct from display gamma).
    rgb = pow(max(rgb, vec3(0.0)), vec3(uSensorGamma));

    // 3. Soft saturation knee. At uSatKnee=0 this is a no-op; at 1 it's
    //    the full Reinhard x / (1 + x). Mixed in proportional to the knee.
    if (uSatKnee > 0.001) {
        vec3 soft = rgb / (1.0 + rgb);
        // Reinhard maps [0,inf) → [0,1) but compresses [0,1] to [0,0.5].
        // Re-expand by 2.0 so unity stays roughly unity at full knee.
        soft *= 2.0;
        rgb = mix(rgb, soft, uSatKnee);
    }

    // 4. RGB cross-coupling (per-pass mixing matrix).
    //    At uColorCross=0 → identity. At 1 → all channels become the avg.
    //    Smooth in between via a one-parameter interpolation.
    if (uColorCross > 0.001) {
        float avg = (rgb.r + rgb.g + rgb.b) / 3.0;
        rgb = mix(rgb, vec3(avg), uColorCross);
    }

    return vec4(rgb, c.a);
}
