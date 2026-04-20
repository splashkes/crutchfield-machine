// layers/contrast.glsl
// Nonlinear electronic response — the "reaction" term in a reaction-diffusion
// reading of the system. Values near mid-grey get pushed toward 0 or 1.
//
//   uContrast : slope at mid-grey (1.0 = identity; >1 steepens; <1 flattens)

vec4 contrast_apply(vec4 c) {
    return vec4(clamp((c.rgb - 0.5) * uContrast + 0.5, 0.0, 1.0), c.a);
}
