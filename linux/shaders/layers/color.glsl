// layers/color.glsl
// Per-pass rotation through colour space. Composes into the "rainbow through
// feedback" effect — a slow orbit in HSV coupled to spatial pattern formation.
//
//   uHueRate : hue increment per pass (0..1 wraps full rainbow)
//   uSatGain : saturation multiplier (>1 pushes toward pure colour)
//
// Field B runs with negated hue rate so the two coupled loops drift in
// opposite colour directions.

vec4 color_apply(vec4 c) {
    float hueRate = (uFieldId == 0) ? uHueRate : -uHueRate;
    vec3 hsv = rgb2hsv(c.rgb);
    hsv.x = fract(hsv.x + hueRate);
    hsv.y = clamp(hsv.y * uSatGain, 0.0, 1.0);
    return vec4(hsv2rgb(hsv), c.a);
}
