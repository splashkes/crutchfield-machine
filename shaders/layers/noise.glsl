// layers/noise.glsl
// Thermal sensor floor.
//
//   uNoiseQuality : 0 = white noise (uniform per-pixel)
//                   1 = pink noise (4 octaves, -6dB/octave — matches real CMOS)

vec4 noise_apply(vec4 c, vec2 uv, float t, uint f) {
    if (uNoiseQuality == 0) {
        float n  = hash21(uv * uRes,        t + float(f) * 0.017) - 0.5;
        float nR = hash21(uv * uRes + 1.1,  t + float(f) * 0.013) - 0.5;
        float nG = hash21(uv * uRes + 2.3,  t + float(f) * 0.019) - 0.5;
        float nB = hash21(uv * uRes + 3.7,  t + float(f) * 0.023) - 0.5;
        vec3 nv = 0.5 * vec3(n) + 0.5 * vec3(nR, nG, nB);
        return vec4(c.rgb + nv * uNoise, c.a);
    }

    // Pink noise: sum multiple octaves of value noise with -6dB/octave rolloff.
    // That gives 1/f^2 power spectrum which more closely matches real sensor
    // noise than flat white noise.
    vec3 nv  = vec3(0.0);
    float nrm = 0.0;
    float amp = 1.0;
    float frq = 0.5;
    for (int o = 0; o < 4; o++) {
        float nR = hash21(uv * uRes * frq + 1.1, t + float(f) * 0.013) - 0.5;
        float nG = hash21(uv * uRes * frq + 2.3, t + float(f) * 0.019) - 0.5;
        float nB = hash21(uv * uRes * frq + 3.7, t + float(f) * 0.023) - 0.5;
        nv  += amp * vec3(nR, nG, nB);
        nrm += amp;
        amp *= 0.5;
        frq *= 2.0;
    }
    return vec4(c.rgb + (nv / nrm) * uNoise, c.a);
}
