// layers/decay.glsl
// Exponential bleed to black. Without this, saturating contrast drives the
// image to pure white or pure colour. With it, transients can decay to
// attractors, including the trivial black fixed point.
//
//   uDecay          : per-pass multiplier (1.0 = no decay; 0.99 = ~100-frame half-life)
//   uBorderSize     : thickness of a soft frame measured from the screen edge
//   uBorderSoftness : width of the transition into the frame
//   uBorderDecay    : extra multiplier at/behind the frame edge

float border_frame_mask(vec2 uv) {
    float aspect = uRes.x / max(uRes.y, 1.0);
    vec2 p = uv * 2.0 - 1.0;
    p.x *= aspect;

    // Rounded-rectangle signed distance. The prior min(edge distance)
    // frame had a visible 45-degree ridge in the corner transition; this
    // gives the sink a continuous rounded corner instead.
    float inset = clamp(uBorderSize, 0.0, 0.45);
    float soft = max(uBorderSoftness, 0.0001);
    float radius = min(max(inset + soft * 0.75, 0.035), 0.45);
    vec2 halfSize = vec2(aspect, 1.0) * (1.0 - inset) - vec2(radius);
    halfSize = max(halfSize, vec2(0.001));
    vec2 q = abs(p) - halfSize;
    float sdf = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - radius;
    return smoothstep(-soft * 2.0, soft * 2.0, sdf);
}

vec4 decay_apply(vec4 c, vec2 uv) {
    float frame = border_frame_mask(uv);
    float borderMul = mix(1.0, clamp(uBorderDecay, 0.0, 1.0), frame);
    return vec4(c.rgb * uDecay * borderMul, c.a);
}
