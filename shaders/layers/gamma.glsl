// layers/gamma.glsl
// Visible tone transfer curve.
//
//   uGamma : 1.0 disables effect; lower values darken, higher values brighten.
//
// The functions keep the historical paired call sites, but gamma_in is now
// identity so the layer is audible/visible even when no later tone stage is on.

vec4 gamma_in_apply(vec4 c) {
    return c;
}

vec4 gamma_out_apply(vec4 c) {
    return vec4(pow(max(c.rgb, vec3(0.0)), vec3(1.0 / max(uGamma, 0.001))), c.a);
}
