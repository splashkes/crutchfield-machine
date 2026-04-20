// layers/gamma.glsl
// CRT/camera transfer curve. Processing the loop in linear light changes the
// dynamics meaningfully — blur and blends behave differently in gamma space.
//
//   uGamma : display gamma (typical CRT ≈ 2.2; sRGB ≈ 2.2; 1.0 disables effect)
//
// Applied as a pair: gamma_in_apply right after sampling, gamma_out_apply
// before any mixes with external (display-gamma) signals.

vec4 gamma_in_apply(vec4 c) {
    return vec4(pow(max(c.rgb, vec3(0.0)), vec3(uGamma)), c.a);
}

vec4 gamma_out_apply(vec4 c) {
    return vec4(pow(max(c.rgb, vec3(0.0)), vec3(1.0 / uGamma)), c.a);
}
