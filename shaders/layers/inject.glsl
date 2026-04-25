// layers/inject.glsl
// Perturbation / initial-condition injection. When active, blends a synthetic
// pattern into the current field. Held momentarily by the space key.
//
//   uInject  : injection strength (0.0 = no effect; 1.0 = replace)
//   uPattern : which pattern to inject
//     0 = horizontal bars     1 = vertical bars
//     2 = centre dot          3 = checker
//     4 = gradient
//   uShapeInject : held shape-inject strength, independent of uInject decay
//   uShapeKind   : 0=triangle 1=star 2=circle 3=square
//   uShapeCount  : 1..16 evenly spaced copies

vec3 pattern_gen(vec2 uv, int id) {
    if (id == 0) return vec3(step(0.5, fract(uv.y * 16.0)));
    if (id == 1) return vec3(step(0.5, fract(uv.x * 16.0)));
    if (id == 2) return vec3(step(length(uv - 0.5), 0.03));
    if (id == 3) { vec2 g = floor(uv * 16.0);
                   return vec3(mod(g.x + g.y, 2.0)); }
    if (id == 4) return vec3(uv.x, uv.y, 1.0 - uv.x);
    return vec3(0.0);
}

float triangle_mask(vec2 q) {
    vec2 a = vec2( 0.0,  0.86);
    vec2 b = vec2(-0.78, -0.58);
    vec2 c = vec2( 0.78, -0.58);
    float e0 = (q.x - a.x) * (b.y - a.y) - (q.y - a.y) * (b.x - a.x);
    float e1 = (q.x - b.x) * (c.y - b.y) - (q.y - b.y) * (c.x - b.x);
    float e2 = (q.x - c.x) * (a.y - c.y) - (q.y - c.y) * (a.x - c.x);
    float inside = step(e0, 0.0) * step(e1, 0.0) * step(e2, 0.0);
    float edge = min(min(abs(e0) / length(b - a), abs(e1) / length(c - b)),
                     abs(e2) / length(a - c));
    return inside * smoothstep(0.0, 0.035, edge);
}

float shape_mask(vec2 q, int kind) {
    if (kind == 0) return triangle_mask(q);
    if (kind == 1) {
        float a = atan(q.y, q.x);
        float r = length(q);
        float starR = 0.54 + 0.24 * cos(5.0 * a);
        return smoothstep(starR, starR - 0.035, r);
    }
    if (kind == 2) return smoothstep(1.0, 0.965, length(q));
    float d = 1.0 - max(abs(q.x), abs(q.y));
    return smoothstep(0.0, 0.035, d);
}

vec3 shape_gen(vec2 uv, out float mask) {
    int count = clamp(uShapeCount, 1, 16);
    float fc = float(count);
    float cols = ceil(sqrt(fc));
    float rows = ceil(fc / cols);
    float cell = min(1.0 / cols, 1.0 / rows);
    float m = 0.0;
    vec3 col = vec3(0.0);

    for (int i = 0; i < 16; i++) {
        if (i >= count) break;
        float fi = float(i);
        float x = mod(fi, cols);
        float y = floor(fi / cols);
        vec2 center = vec2((x + 0.5) / cols, (y + 0.5) / rows);
        vec2 q = (uv - center) / (cell * 0.34);
        float sm = shape_mask(q, uShapeKind);
        vec3 sc = hsv2rgb(vec3(fract(0.10 + fi * 0.113), 0.55, 1.0));
        col = mix(col, sc, sm * (1.0 - m));
        m = max(m, sm);
    }

    mask = clamp(m, 0.0, 1.0);
    return col;
}

vec4 inject_apply(vec4 c, vec2 uv) {
    vec3 rgb = mix(c.rgb, pattern_gen(uv, uPattern), uInject);
    float shapeMask = 0.0;
    vec3 shape = shape_gen(uv, shapeMask);
    rgb = mix(rgb, shape, shapeMask * uShapeInject);
    return vec4(rgb, c.a);
}
