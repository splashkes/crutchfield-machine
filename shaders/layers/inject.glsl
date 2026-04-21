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

vec4 inject_apply(vec4 c, vec2 uv) {
    vec4 p = pattern_gen(uv, uPattern);
    return vec4(mix(c.rgb, p.rgb, uInject * p.a), c.a);
}
