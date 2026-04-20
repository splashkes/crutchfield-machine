// layers/noise.glsl
// Thermal sensor noise floor. On physical rigs this is the reason loops never
// truly reach a fixed point: every frame the camera injects a few bits of
// entropy at the LSB. Without this the simulation can get stuck.
//
//   uNoise : noise amplitude (0.0..~0.05 is a reasonable range)

vec4 noise_apply(vec4 c, vec2 uv, float t, uint f) {
    float n = hash21(uv * uRes, t + float(f) * 0.017) - 0.5;
    // Independent R,G,B would desaturate noise → use a small correlated term
    // plus a small per-channel term, matching real sensor behaviour.
    float nR = hash21(uv * uRes + 1.1, t + float(f) * 0.013) - 0.5;
    float nG = hash21(uv * uRes + 2.3, t + float(f) * 0.019) - 0.5;
    float nB = hash21(uv * uRes + 3.7, t + float(f) * 0.023) - 0.5;
    vec3 nv = 0.5 * vec3(n) + 0.5 * vec3(nR, nG, nB);
    return vec4(c.rgb + nv * uNoise, c.a);
}
