// layers/inject.glsl
// Perturbation / initial-condition injection. When active, blends a synthetic
// pattern into the current field. Held momentarily by the space key.
//
//   uInject  : injection strength (0.0 = no effect; 1.0 = replace)
//   uPattern : which pattern to inject
//     0 = horizontal bars     1 = vertical bars
//     2 = centre dot          3 = checker
//     4 = gradient

vec3 pattern_gen(vec2 uv, int id) {
    if (id == 0) return vec3(step(0.5, fract(uv.y * 16.0)));
    if (id == 1) return vec3(step(0.5, fract(uv.x * 16.0)));
    if (id == 2) return vec3(step(length(uv - 0.5), 0.03));
    if (id == 3) { vec2 g = floor(uv * 16.0);
                   return vec3(mod(g.x + g.y, 2.0)); }
    if (id == 4) return vec3(uv.x, uv.y, 1.0 - uv.x);
    return vec3(0.0);
}

vec4 inject_apply(vec4 c, vec2 uv) {
    return vec4(mix(c.rgb, pattern_gen(uv, uPattern), uInject), c.a);
}
