// layers/optics.glsl
// Lens-like sampling: anisotropic 5-tap blur + radial chromatic aberration.
// These two belong together because in real optics they both come from
// wavelength-dependent focusing — CA is just per-channel defocus.
//
//   uBlurX     : blur radius along primary axis (in pixels)
//   uBlurY     : blur radius along secondary axis (in pixels)
//   uBlurAngle : rotation of the blur ellipse
//   uChroma    : radial chromatic aberration strength (R inward, B outward)

vec4 optics_sample(sampler2D tex, vec2 src) {
    vec2 px = 1.0 / uRes;

    float bc = cos(uBlurAngle), bs = sin(uBlurAngle);
    vec2 bx = vec2( bc,  bs) * uBlurX * px;
    vec2 by = vec2(-bs,  bc) * uBlurY * px;

    // Radial direction from image centre for CA offset.
    vec2 rad = (src - vec2(0.5));
    vec2 ca  = rad * uChroma;

    // 5-tap cross pattern along the (possibly rotated) blur axes.
    vec2 off[5];
    off[0] = vec2(0.0);
    off[1] =  bx;
    off[2] = -bx;
    off[3] =  by;
    off[4] = -by;

    vec3 col = vec3(0.0);
    for (int k = 0; k < 5; k++) {
        col.r += texture(tex, src + off[k] - ca).r;
        col.g += texture(tex, src + off[k]     ).g;
        col.b += texture(tex, src + off[k] + ca).b;
    }
    return vec4(col / 5.0, 1.0);
}
