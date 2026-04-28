// layers/contrast.glsl
// Nonlinear electronic response — the "reaction" term in a reaction-diffusion
// reading of the system. Values near mid-grey get pushed toward 0 or 1.
//
//   uContrast : slope at mid-grey (1.0 = identity; >1 steepens; <1 flattens)
//
// Deliberately unclamped. A steep S-curve will overshoot [0,1]; preserving
// the overshoot keeps high-dynamic-range shaping available to downstream
// stages. The `decay` layer is the correct tool for pulling runaway
// values back toward an attractor. See development/LAYERS.md
// §float-precision invariant and ADR-0015.

vec4 contrast_apply(vec4 c) {
    return vec4((c.rgb - 0.5) * uContrast + 0.5, c.a);
}
