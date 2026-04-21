// layers/pixelate.glsl
// Spatial quantization of the feedback SAMPLE. Takes over from `optics_sample`
// when uPixelateStyle != 0. See ADR-0015 / development/LAYERS.md §Sample.
//
//   uPixelateStyle     : 0 = off; 1..9 = (shape × size)
//                        1..3 dots     (small / med / large cells)
//                        4..6 squares  (small / med / large)
//                        7..9 rounded  (small / med / large — squircle)
//
//   uPixelateBleedIdx  : 0 = off (rigid — no bleed)
//                        1 = soft    (edge glow only; no jitter/chroma/vig)
//                        2 = CRT     (strong vignette + chromatic; still, Trinitron)
//                        3 = melt    (big sinusoidal jitter — pixels swim)
//                        4 = fried   (massive time-varying chromatic + strobing vignette)
//                        5 = burned  (dead/stuck-colour pixel groups — reseed via action)
//   uPixelateBurnSeed  : integer that re-rolls the burn pattern when incremented.
//
// Cell sizes: 8 / 16 / 32 texels. Texels (not UV) so the look stays
// consistent across sim resolutions.

// Preset decoder — returns per-aspect strength:
//   jit  : cell-sample-position jitter amplitude (0..1 = 0..25% of cell)
//   chr  : R/B horizontal offset in screen pixels
//   edge : dot/rounded mask softening (widens smoothstep range)
//   vig  : intra-cell vignette (brighter centre, dimmer edges). >1 crushes.
//
// Each preset picks ONE dominant aspect so stepping through reads as
// categorically different looks, not "more/less of the same softness".
void pixelate_decode_bleed(int idx, out float jit, out float chr,
                           out float edge, out float vig) {
    if (idx <= 0)      { jit = 0.0;  chr = 0.0;  edge = 0.0;  vig = 0.0; }

    // soft — only edge glow. Other aspects off. Shapes dilate gently into
    // gaps; no shimmer, no RGB split, no vignette.
    else if (idx == 1) { jit = 0.0;  chr = 0.0;  edge = 0.85; vig = 0.0; }

    // CRT — phosphor-dot feel: strong vignette + obvious chromatic. No
    // jitter (stable monitor). Each cell reads as a lit pixel with dark
    // edges and a visible RGB triad.
    else if (idx == 2) { jit = 0.0;  chr = 0.85; edge = 0.15; vig = 1.6; }

    // melt — motion is the whole story. Big sinusoidal jitter, no chroma,
    // soft edges so the wobble smears between cells. Pixels swim.
    else if (idx == 3) {
        float w = 0.5 + 0.5 * sin(uTime * 1.3);
        jit = 0.55 + 0.65 * w;    // 0.55..1.20 — ~14-30% of cell shake
        chr = 0.0;
        edge = 0.65;
        vig = 0.0;
    }

    // fried — massive, time-varying chromatic drift with strobing vignette.
    // RGB channels split by 10-20 px and reconverge; cells flash.
    else if (idx == 4) {
        float p = 0.5 + 0.5 * sin(uTime * 3.1);    // fast pulse
        float q = 0.5 + 0.5 * sin(uTime * 0.7);    // slow pulse
        jit = 0.25;
        chr = 2.0 + 2.5 * p;                        // maps to 10-22 px
        edge = 0.20;
        vig = 0.20 + 1.4 * q;                       // 0.2..1.6 strobing
    }

    // burned — a textured baseline so the burns read against a live field.
    // The burn logic itself lives in pixelate_apply() because it needs
    // cell coords and the uPixelateBurnSeed uniform.
    else {
        jit = 0.10; chr = 1.0; edge = 0.25; vig = 0.35;
    }
}

// Returns a colour to replace the cell sample with, or vec3(-1) if this
// cell is not burned. Groups are 2×2 cell clusters so burns appear as
// recognizable patches rather than single-cell speckle. The seed moves
// the whole burn pattern — Alt+Delete increments it on the host side.
vec3 pixelate_burn_color(vec2 cell) {
    vec2 group  = floor(cell / 2.0);
    float seedF = float(uPixelateBurnSeed) * 0.01717;
    float roll  = hash21(group, seedF);
    if (roll < 0.93) return vec3(-1.0);         // most groups are NOT burned

    // Seed-wide tint — every burned group in a given seed leans toward the
    // same hue ("tending towards a specific color"). Re-seeding picks a
    // new hue, and the ~7% burned groups all shift with it.
    float tintHue = fract(float(uPixelateBurnSeed) * 0.2718 + 0.137);
    vec3  tint    = hsv2rgb(vec3(tintHue, 0.85, 1.0));

    float flavor = hash21(group + vec2(7.3, 3.1), seedF);
    if (flavor < 0.50) return vec3(0.0);        // dead black
    return tint * 1.15;                          // stuck-at-colour (slightly over-range)
}

vec4 pixelate_apply(sampler2D tex, vec2 src_uv_pixel, vec2 uv) {
    int idx   = uPixelateStyle - 1;
    int shape = idx / 3;              // 0 dots, 1 squares, 2 rounded
    int size  = idx - shape * 3;      // 0 small, 1 med, 2 large

    // Cell sizes scale with sim resolution so "small" reads the same on
    // 720p and 4K. Reference: 1280-wide sim → 8/16/32-texel cells. At 4K
    // those become 24/48/96 px, keeping the perceptual size constant.
    float ref   = uRes.x / 1280.0;
    float cellPx = ((size == 0) ? 8.0 : (size == 1) ? 16.0 : 32.0) * ref;
    vec2  cellUV = vec2(cellPx) / uRes;

    vec2 cell    = floor(uv / cellUV);
    vec2 cell_uv = (cell + 0.5) * cellUV;

    // Re-apply warp/thermal to cell centre so pixelation propagates through
    // the dynamics (see LAYERS.md for why).
    vec2 cell_src = cell_uv;
    if ((uEnable & L_WARP)    != 0) cell_src = warp_apply(cell_src);
    if ((uEnable & L_THERMAL) != 0) cell_src = thermal_warp(cell_src);

    // CRT bleed — four composable aspects, driven by uPixelateBleedIdx.
    float bJit, bChr, bEdge, bVig;
    pixelate_decode_bleed(uPixelateBleedIdx, bJit, bChr, bEdge, bVig);

    // 1. Per-cell temporal jitter. Hash on (cell, frame-bucket) so nearby
    //    cells shimmer independently, and the shimmer pattern changes over
    //    time. Amplitude in % of cell size: at bJit = 1.0, jitter reaches
    //    ±25% of the cell — visible shake without cells colliding.
    vec2 jitSeed = cell + vec2(floor(uTime * 20.0), 0.0);
    vec2 jit = vec2(hash21(jitSeed,                 uTime * 0.13),
                    hash21(jitSeed + vec2(7.3, 2.1), uTime * 0.17)) - 0.5;
    vec2 cell_src_j = cell_src + jit * cellUV * bJit;

    // 2. Chromatic subpixel offset — R/B fetched with a small horizontal
    //    shift, G centred. Expressed in screen pixels (scaled via uRes)
    //    rather than cell-relative so chromatic aberration reads as a
    //    global CRT property instead of tiny-cells vanishing into sub-pixel.
    //    At bChr = 1.0 → 5 px shift; bChr = 0.25 (soft) → 1.25 px.
    float chOff = (5.0 / uRes.x) * bChr;
    vec3 block;
    if (bChr > 0.001) {
        block.r = texture(tex, cell_src_j + vec2(-chOff, 0.0)).r;
        block.g = texture(tex, cell_src_j                    ).g;
        block.b = texture(tex, cell_src_j + vec2( chOff, 0.0)).b;
    } else {
        block = texture(tex, cell_src_j).rgb;
    }

    // 4. Intra-cell vignette — brighter centre, dimmer edges. Gives each
    //    block a phosphor-dot feel. dEdge ∈ [0,1] with 0 at cell centre.
    //    For shape == dots/rounded this also applies; gap pixels ignore it
    //    so per-pixel background isn't darkened.
    vec2 local = (uv - cell_uv) / (cellUV * 0.5);  // [-1, +1]
    float dEdge = max(abs(local.x), abs(local.y));
    float vignette = 1.0 - bVig * 0.5 * dEdge * dEdge;
    block *= vignette;

    // 5. Burn pass — only active when bleed preset == burned (idx 5).
    //    Replaces the cell's block colour with a seed-tinted burn or
    //    dead-black if this group rolled "burned". Happens AFTER vignette
    //    so burn colours aren't darkened at cell edges.
    if (uPixelateBleedIdx == 5) {
        vec3 burn = pixelate_burn_color(cell);
        if (burn.r >= 0.0) block = burn;
    }

    if (shape == 1) {
        // Hard squares — entire cell returns the vignetted, jittered,
        // chromatic-shifted sample.
        return vec4(block, 1.0);
    }

    // Dots / rounded — gap pixels sample at their own src_uv so the fine
    // detail between shapes keeps evolving. Gaps skip the vignette.
    vec4 gap = texture(tex, src_uv_pixel);

    // 3. Softer edges when bEdge > 0. Widens the smoothstep range so
    //    shapes glow into gaps instead of stamping hard.
    float mask;
    if (shape == 0) {
        float d = length(local);
        float lo = mix(0.85, 0.55, clamp(bEdge, 0.0, 1.0));
        float hi = mix(0.95, 1.05, clamp(bEdge, 0.0, 1.0));
        mask = 1.0 - smoothstep(lo, hi, d);
    } else {
        float sq = pow(abs(local.x), 4.0) + pow(abs(local.y), 4.0);
        float lo = mix(0.88, 0.50, clamp(bEdge, 0.0, 1.0));
        float hi = mix(1.00, 1.10, clamp(bEdge, 0.0, 1.0));
        mask = 1.0 - smoothstep(lo, hi, sq);
    }
    return vec4(mix(gap.rgb, block, mask), 1.0);
}
