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
    float d = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    float outer = max(uBorderSize, 0.0);
    float inner = outer + max(uBorderSoftness, 0.0001);
    return 1.0 - smoothstep(outer, inner, d);
}

vec4 decay_apply(vec4 c, vec2 uv) {
    float frame = border_frame_mask(uv);
    float borderMul = mix(1.0, clamp(uBorderDecay, 0.0, 1.0), frame);
    return vec4(c.rgb * uDecay * borderMul, c.a);
}
