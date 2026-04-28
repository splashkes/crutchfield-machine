// layers/noise.glsl
// Sensor-floor archetypes. uNoise controls overall amplitude; uNoiseQuality
// picks the character of the noise.
//
//   uNoiseQuality : 0 = white        (uniform per-pixel, balanced)
//                   1 = pink 1/f     (4 octaves, -6dB/oct — real-CMOS-ish)
//                   2 = heavy static ("broken TV" — coarse high-contrast grain)
//                   3 = VCR          (rolling horizontal band + chroma bleed)
//                   4 = dropout      (sparse digital block corruption)
//
// All modes respect uNoise as amplitude. At uNoise = 0 the output equals
// the input. Modes 2-4 apply internal multipliers so their characteristic
// look reads at the same uNoise range as modes 0-1.

vec4 noise_apply(vec4 c, vec2 uv, float t, uint f) {
    // ── 0: white noise ────────────────────────────────────────────
    if (uNoiseQuality == 0) {
        float n  = hash21(uv * uRes,        t + float(f) * 0.017) - 0.5;
        float nR = hash21(uv * uRes + 1.1,  t + float(f) * 0.013) - 0.5;
        float nG = hash21(uv * uRes + 2.3,  t + float(f) * 0.019) - 0.5;
        float nB = hash21(uv * uRes + 3.7,  t + float(f) * 0.023) - 0.5;
        vec3 nv = 0.5 * vec3(n) + 0.5 * vec3(nR, nG, nB);
        return vec4(c.rgb + nv * uNoise, c.a);
    }

    // ── 1: pink 1/f ───────────────────────────────────────────────
    // Sum multiple octaves of value noise with -6dB/oct rolloff → 1/f²
    // power spectrum; closer to real sensor noise than white.
    if (uNoiseQuality == 1) {
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

    // ── 2: heavy static ───────────────────────────────────────────
    // Coarse ~3-texel cells with per-cell hash. Per-cell values pushed
    // toward extremes so grain reads as chunky black/white flashes plus
    // colour speckles — the "broken analog TV" look. Aggressive gain
    // (~8×) so the mode reads at the default uNoise = 0.002 range. If
    // you want subtle per-pixel grain use mode 0 or 1 instead.
    if (uNoiseQuality == 2) {
        vec2 cell = floor(uv * uRes / 3.0);
        float n1 = hash21(cell,              t * 3.0 + float(f) * 0.031);
        float n2 = hash21(cell + vec2(7,11), t * 3.0 + float(f) * 0.037);
        float n3 = hash21(cell + vec2(3, 5), t * 3.0 + float(f) * 0.041);
        // Smoothstep-style bias toward 0/1 extremes.
        n1 = n1 * n1 * (3.0 - 2.0 * n1);
        float lumaSpike = (n1 > 0.85) ? 1.2 : (n1 < 0.15 ? -1.0 : 0.0);
        vec3 nv = vec3(n1 - 0.5, n2 - 0.5, n3 - 0.5) * 0.6
                + vec3(lumaSpike) * 0.7;
        return vec4(c.rgb + nv * uNoise * 8.0, c.a);
    }

    // ── 3: VCR tracking ───────────────────────────────────────────
    // Rolling horizontal bright band (loops every ~7 s) + per-row
    // chroma-heavy noise that reads as horizontal colour streaking.
    // No clamps — bar contribution can over-drive and feed back as
    // light pulses through the loop. Aggressive gain (~12×) so the
    // rolling bar and chroma bleed are plainly visible at modest
    // uNoise values (0.005+).
    if (uNoiseQuality == 3) {
        float y = uv.y;
        // Rolling bar position: slow vertical slide, wraps.
        float rollY = fract(t * 0.14);
        // Distance to bar with wraparound on the unit interval.
        float barDist = min(min(abs(y - rollY),
                                abs(y - rollY + 1.0)),
                                abs(y - rollY - 1.0));
        float bar = smoothstep(0.10, 0.0, barDist);      // 1 inside, 0 outside
        // Per-row seed → chroma fringe. Row grain is coarser than per-pixel;
        // uv.x perturbation introduces short horizontal smear.
        float rowSeed = floor(y * uRes.y * 0.5);
        float rowJit  = hash21(vec2(rowSeed, floor(t * 8.0)), float(f) * 0.09) - 0.5;
        float hx      = uv.x * uRes.x * 0.08 + rowJit * 3.0;
        float smearR = hash21(vec2(hx + 0.31, rowSeed), float(f) * 0.013) - 0.5;
        float smearB = hash21(vec2(hx - 0.27, rowSeed), float(f) * 0.017) - 0.5;
        // Less green jitter — "chroma bleed" reads as RB.
        float smearG = (hash21(vec2(hx, rowSeed + 1.0), float(f) * 0.011) - 0.5) * 0.25;
        float bandAmp = 0.6 + bar * 3.0;
        vec3 chroma   = vec3(smearR, smearG, smearB) * bandAmp;
        // Bar luma boost — the visible rolling bright line.
        vec3 barBoost = vec3(bar * 2.5);
        return vec4(c.rgb + (chroma + barBoost) * uNoise * 12.0, c.a);
    }

    // ── 4: dropout ────────────────────────────────────────────────
    // Sparse block corruption. The *base* layer is the same as before —
    // ~5% of 24-texel blocks flip to a stuck colour, driven by uNoise.
    //
    // On top of that, the music engine publishes per-trigger envelopes
    // (uMusKick / uMusSnare / uMusHat / uMusBass / uMusOther). Each one
    // stamps a DIFFERENT flavour of corruption while its envelope is
    // warm, so a kick, snare, hat etc. each glitch the image in a
    // recognisable way. See audio.cpp::classify_sample() for the bucketing.
    //
    //   kick  → big wide blocks pulled to black ("hole punch")
    //   snare → sharp narrow white flashes
    //   hat   → tiny high-frequency speckle (small cells, green-ish)
    //   bass  → medium blocks tinted toward a rotating hue
    //   other → rare wide rainbow glitch
    //
    // All music contributions ride on top of whatever the base uNoise
    // dropout already produced, so cranking uNoise still gives the same
    // baseline while the music adds rhythmic punctuation.
    vec3 result = c.rgb;

    // Base dropout layer (unchanged).
    vec2 cell = floor(uv * uRes / 24.0);
    float cellRand = hash21(cell, floor(t * 4.0) + float(f / 30u));
    if (cellRand > 0.95) {
        vec3 corr;
        float g = hash21(cell + vec2(3.0, 7.0), t);
        if      (g < 0.33) corr = vec3(1.0, 0.0, 1.0);
        else if (g < 0.66) corr = vec3(0.0, 1.0, 0.0);
        else               corr = vec3(hash21(cell,               t),
                                       hash21(cell + vec2(0.5),  t),
                                       hash21(cell + vec2(1.0),  t));
        result = mix(result, corr, clamp(uNoise * 40.0, 0.0, 1.0));
    } else {
        float bg = hash21(uv * uRes, t + float(f) * 0.011) - 0.5;
        result += vec3(bg) * uNoise * 0.3;
    }

    // Music-driven flavours. Each block uses a time bucket coarser than
    // the base dropout so the pulses read as "on the beat" rather than
    // per-frame static.
    if (uMusKick > 0.02) {
        // KICK — wide 48-texel blocks pulled toward black. Big, rare,
        // heavy holes that sync with the bass drum.
        vec2 kc = floor(uv * uRes / 48.0);
        float r = hash21(kc, floor(t * 8.0));
        float thresh = 0.75 - uMusKick * 0.35;   // denser at higher envelope
        if (r > thresh) {
            result = mix(result, vec3(0.0), clamp(uMusKick * 1.5, 0.0, 1.0));
        }
    }
    if (uMusSnare > 0.02) {
        // SNARE — narrow 10-texel cells, bright white flashes, high
        // frequency so they read as a sharp crack rather than a roll.
        vec2 sc = floor(uv * uRes / 10.0);
        float r = hash21(sc + vec2(3.1, 0.0), floor(t * 20.0));
        float thresh = 0.92 - uMusSnare * 0.10;
        if (r > thresh) {
            result = mix(result, vec3(1.6, 1.6, 1.5), clamp(uMusSnare, 0.0, 1.0));
        }
    }
    if (uMusHat > 0.02) {
        // HAT — very small 4-texel speckle, slight green tint, shimmery.
        vec2 hc = floor(uv * uRes / 4.0);
        float r = hash21(hc + vec2(7.3, 1.1), floor(t * 30.0));
        float thresh = 0.97 - uMusHat * 0.04;
        if (r > thresh) {
            vec3 tint = vec3(r * 0.4, 1.0, r * 0.3);
            result = mix(result, tint, clamp(uMusHat * 0.9, 0.0, 1.0));
        }
    }
    if (uMusBass > 0.02) {
        // BASS — medium 30-texel blocks tinted toward a hue that rotates
        // with time, so successive bass notes glitch in different colours.
        vec2 bc = floor(uv * uRes / 30.0);
        float r = hash21(bc + vec2(1.7, 4.2), floor(t * 6.0));
        float thresh = 0.88 - uMusBass * 0.12;
        if (r > thresh) {
            vec3 tint = hsv2rgb(vec3(fract(t * 0.3), 0.85, 1.1));
            result = mix(result, tint, clamp(uMusBass, 0.0, 1.0));
        }
    }
    if (uMusOther > 0.02) {
        // OTHER — rare wide rainbow-coloured glitch, 20-texel, so melodic
        // synths still punctuate but sparingly.
        vec2 oc = floor(uv * uRes / 20.0);
        float r = hash21(oc + vec2(9.1, 5.4), floor(t * 12.0));
        float thresh = 0.96 - uMusOther * 0.05;
        if (r > thresh) {
            vec3 tint = vec3(hash21(oc, t), hash21(oc + 0.3, t), hash21(oc + 0.7, t));
            result = mix(result, tint * 1.3, clamp(uMusOther * 0.8, 0.0, 1.0));
        }
    }

    return vec4(result, c.a);
}
