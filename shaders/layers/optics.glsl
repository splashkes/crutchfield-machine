// layers/optics.glsl
// Lens-like sampling: anisotropic blur + radial chromatic aberration.
//
//   uBlurQuality : 0 = 5-tap cross (fast, classic look)
//                   1 = 9-tap 3x3 Gaussian (smoother)
//                   2 = 25-tap 5x5 Gaussian (true diffuse defocus)
//   uCAQuality   : 0 = 3-sample per-channel shift (classic CA)
//                   1 = 5-sample radial ramp
//                   2 = 8-sample wavelength-weighted radial (true dispersion)

vec3 ca_sample(sampler2D tex, vec2 src, vec2 ca) {
    if (uCAQuality == 0) {
        return vec3(texture(tex, src - ca).r,
                    texture(tex, src     ).g,
                    texture(tex, src + ca).b);
    }
    if (uCAQuality == 1) {
        vec3 col = vec3(0.0);
        vec3 nrm = vec3(0.0);
        for (int i = 0; i < 5; i++) {
            float t = (float(i) - 2.0) * 0.5;
            vec4 s  = texture(tex, src + t * ca);
            float wR = max(0.0, 1.0 - abs(t + 0.6));
            float wG = max(0.0, 1.0 - abs(t));
            float wB = max(0.0, 1.0 - abs(t - 0.6));
            col += vec3(wR*s.r, wG*s.g, wB*s.b);
            nrm += vec3(wR, wG, wB);
        }
        return col / max(nrm, vec3(1e-5));
    }
    vec3 col = vec3(0.0);
    vec3 nrm = vec3(0.0);
    for (int i = 0; i < 8; i++) {
        float t = (float(i) - 3.5) / 3.5;
        vec4 s = texture(tex, src + t * ca);
        float wR = exp(-pow((t + 0.75) / 0.45, 2.0));
        float wG = exp(-pow((t       ) / 0.45, 2.0));
        float wB = exp(-pow((t - 0.75) / 0.45, 2.0));
        col += vec3(wR*s.r, wG*s.g, wB*s.b);
        nrm += vec3(wR, wG, wB);
    }
    return col / max(nrm, vec3(1e-5));
}

vec3 blur_kernel_sample(sampler2D tex, vec2 src, vec2 ca, vec2 bx, vec2 by) {
    vec3 col = vec3(0.0);
    float wsum = 0.0;

    if (uBlurQuality == 0) {
        vec2 o[5];
        o[0] = vec2(0); o[1] = bx; o[2] = -bx; o[3] = by; o[4] = -by;
        for (int k = 0; k < 5; k++) {
            col  += ca_sample(tex, src + o[k], ca);
            wsum += 1.0;
        }
    } else if (uBlurQuality == 1) {
        float w[9] = float[9](1., 2., 1.,  2., 4., 2.,  1., 2., 1.);
        int idx = 0;
        for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            vec2 off = float(i) * bx + float(j) * by;
            col  += w[idx] * ca_sample(tex, src + off, ca);
            wsum += w[idx];
            idx++;
        }
    } else {
        float w[25] = float[25](
             1.,  4.,  6.,  4., 1.,
             4., 16., 24., 16., 4.,
             6., 24., 36., 24., 6.,
             4., 16., 24., 16., 4.,
             1.,  4.,  6.,  4., 1.);
        int idx = 0;
        for (int j = -2; j <= 2; j++)
        for (int i = -2; i <= 2; i++) {
            vec2 off = float(i) * bx + float(j) * by;
            col  += w[idx] * ca_sample(tex, src + off, ca);
            wsum += w[idx];
            idx++;
        }
    }
    return col / wsum;
}

vec4 optics_sample(sampler2D tex, vec2 src) {
    vec2 px = 1.0 / uRes;

    float bc = cos(uBlurAngle), bs = sin(uBlurAngle);
    vec2 ax = vec2( bc,  bs);
    vec2 ay = vec2(-bs,  bc);
    vec2 blurBx = ax * max(uBlurX, 0.0) * px;
    vec2 blurBy = ay * max(uBlurY, 0.0) * px;
    vec2 sharpBx = ax * max(-uBlurX, 0.0) * px;
    vec2 sharpBy = ay * max(-uBlurY, 0.0) * px;

    vec2 rad = (src - vec2(0.5));
    vec2 ca  = rad * uChroma;

    vec3 col = blur_kernel_sample(tex, src, ca, blurBx, blurBy);
    float sharpAmt = max(max(-uBlurX, 0.0), max(-uBlurY, 0.0));
    if (sharpAmt > 0.0) {
        vec3 base = ca_sample(tex, src, ca);
        vec3 soft = blur_kernel_sample(tex, src, ca, sharpBx, sharpBy);
        float strength = clamp(sharpAmt * 0.28, 0.0, 2.0);
        col = clamp(col + (base - soft) * strength, 0.0, 1.0);
    }

    return vec4(col, 1.0);
}
