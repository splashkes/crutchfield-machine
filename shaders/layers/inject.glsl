// layers/inject.glsl
// Perturbation / initial-condition injection. When active, blends a synthetic
// pattern into the current field. Held momentarily by the space key.
//
//   uInject  : injection strength (0.0 = no effect; 1.0 = replace)
//   uPattern : which pattern to inject
//     0 = horizontal bars       1 = vertical bars
//     2 = centre dot            3 = checker
//     4 = gradient              5 = noise field (random RGB)
//     6 = concentric rings      7 = spiral
//     8 = polka dots            9 = starburst / radial rays
//     10 = bouncer (animated — low-res pong box, bounces top↔bottom)
//
// pattern_gen returns vec4(rgb, alpha). Alpha is the per-pixel injection
// mask: 1.0 = fully inject this pixel, 0.0 = leave col unchanged. Static
// patterns return 1.0 everywhere so they behave the same as before.
// Sparse / animated patterns (like the bouncer) return 0.0 outside their
// shape so the feedback field keeps evolving around them instead of
// getting wiped by an all-black pattern background.
//
//   uPatternInject : persistent low-stack pattern layer amount
//   uShapeInject   : held shape-inject strength, independent of uInject decay
//   uShapeKind     : 0=triangle 1=star 2=circle 3=square
//   uShapeCount    : 1..16 evenly spaced copies
//   uShapeSize     : size multiplier
//   uShapeAngle    : rotation in radians

vec4 pattern_gen(vec2 uv, int id) {
    if (id == 0) return vec4(vec3(step(0.5, fract(uv.y * 16.0))), 1.0);
    if (id == 1) return vec4(vec3(step(0.5, fract(uv.x * 16.0))), 1.0);
    if (id == 2) return vec4(vec3(step(length(uv - 0.5), 0.03)), 1.0);
    if (id == 3) { vec2 g = floor(uv * 16.0);
                   return vec4(vec3(mod(g.x + g.y, 2.0)), 1.0); }
    if (id == 4) return vec4(uv.x, uv.y, 1.0 - uv.x, 1.0);

    if (id == 5) {
        float r = hash21(uv * uRes,             uTime);
        float g = hash21(uv * uRes + vec2(1.7), uTime + 0.3);
        float b = hash21(uv * uRes + vec2(3.1), uTime + 0.7);
        return vec4(r, g, b, 1.0);
    }

    if (id == 6) {
        float d = length(uv - 0.5);
        return vec4(vec3(step(0.5, fract(d * 20.0))), 1.0);
    }

    if (id == 7) {
        vec2 p = uv - 0.5;
        float a  = atan(p.y, p.x);
        float r  = length(p);
        float arm = fract(r * 10.0 - a / 6.2831853 * 2.0);
        return vec4(vec3(step(0.5, arm)), 1.0);
    }

    if (id == 8) {
        vec2 cell = fract(uv * 8.0) - 0.5;
        return vec4(vec3(step(length(cell), 0.18)), 1.0);
    }

    if (id == 9) {
        vec2 p = uv - 0.5;
        float a = atan(p.y, p.x);
        float rays = step(0.5, fract(a / 6.2831853 * 12.0));
        float mask = smoothstep(0.0, 0.04, length(p));
        return vec4(vec3(rays * mask), 1.0);
    }

    // 10: bouncer — low-res box translating top↔bottom on a ~2 s triangle
    // wave. Alpha = 1 only INSIDE the box, 0 elsewhere, so the feedback
    // loop keeps evolving around the ball. Combined with the host-side
    // hold timer (p.injectHoldTimer) this gives a 10-second pong-ball
    // animation that rides on top of the dynamics instead of wiping them.
    if (id == 10) {
        float period = 2.0;
        float phase  = fract(uTime / period);
        // Triangle wave: 0→1→0 per period.
        float y = phase < 0.5 ? 2.0 * phase : 2.0 - 2.0 * phase;
        // Snap y to a coarse grid so motion reads as low-res / quantized.
        float gridN = 20.0;
        y = (floor(y * gridN) + 0.5) / gridN;
        vec2 center = vec2(0.5, y);
        float hs    = 0.025;                     // half-size → ~5% box
        if (abs(uv.x - center.x) < hs && abs(uv.y - center.y) < hs) {
            // Warm white — reads as a hot spot that trails nicely through
            // the feedback dynamics.
            return vec4(1.2, 1.1, 0.7, 1.0);
        }
        return vec4(0.0);
    }

    return vec4(0.0);
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
    float aspect = uRes.x / max(uRes.y, 1.0);
    float m = 0.0;
    vec3 col = vec3(0.0);

    for (int i = 0; i < 16; i++) {
        if (i >= count) break;
        float fi = float(i);
        float x = mod(fi, cols);
        float y = floor(fi / cols);
        vec2 center = vec2((x + 0.5) / cols, (y + 0.5) / rows);
        vec2 q = (uv - center) / (cell * 0.22 * uShapeSize);
        q.x *= aspect;
        float cs = cos(uShapeAngle);
        float sn = sin(uShapeAngle);
        q = mat2(cs, -sn, sn, cs) * q;
        float sm = shape_mask(q, uShapeKind);
        vec3 sc = hsv2rgb(vec3(fract(0.10 + fi * 0.113), 0.55, 1.0));
        col = mix(col, sc, sm * (1.0 - m));
        m = max(m, sm);
    }

    mask = clamp(m, 0.0, 1.0);
    return col;
}

vec4 shape_inject_apply(vec4 c, vec2 uv) {
    vec3 rgb = c.rgb;
    float shapeMask = 0.0;
    vec3 shape = shape_gen(uv, shapeMask);
    rgb = mix(rgb, shape, shapeMask * uShapeInject);
    return vec4(rgb, c.a);
}

vec4 inject_apply(vec4 c, vec2 uv) {
    vec4 p = pattern_gen(uv, uPattern);
    vec3 rgb = mix(c.rgb, p.rgb, uInject * p.a);
    return vec4(rgb, c.a);
}

vec4 pattern_layer_apply(vec4 c, vec2 uv) {
    vec4 p = pattern_gen(uv, uPattern);
    vec3 rgb = mix(c.rgb, p.rgb, clamp(uPatternInject, 0.0, 1.0) * p.a);
    return vec4(rgb, c.a);
}

vec4 full_inject_apply(vec4 c, vec2 uv) {
    vec4 p = pattern_gen(uv, uPattern);
    vec3 rgb = mix(c.rgb, p.rgb, uInject * p.a);
    float shapeMask = 0.0;
    vec3 shape = shape_gen(uv, shapeMask);
    rgb = mix(rgb, shape, shapeMask * uShapeInject);
    return vec4(rgb, c.a);
}
