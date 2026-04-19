// layers/color.glsl
// HSV hue rotation + saturation.
// Fields 0,3 use +hueRate; fields 1,2 use -hueRate. Combined with the
// theta mirror in warp.glsl this gives each of the 4 ring fields a unique
// (theta-sign, hueRate-sign) fingerprint.

vec4 color_apply(vec4 c) {
    float sgn = (uFieldId == 0 || uFieldId == 3) ? 1.0 : -1.0;
    float hueRate = sgn * uHueRate;

    vec3 hsv = rgb2hsv(c.rgb);
    hsv.x = fract(hsv.x + hueRate);
    hsv.y = clamp(hsv.y * uSatGain, 0.0, 1.0);
    return vec4(hsv2rgb(hsv), c.a);
}
