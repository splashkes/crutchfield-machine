// layers/decay.glsl
// Exponential bleed to black. Without this, saturating contrast drives the
// image to pure white or pure colour. With it, transients can decay to
// attractors, including the trivial black fixed point.
//
//   uDecay : per-pass multiplier (1.0 = no decay; 0.99 = ~100-frame half-life)

vec4 decay_apply(vec4 c) {
    return vec4(c.rgb * uDecay, c.a);
}
