#version 460 core
// main.frag — orchestrator. Each layer lives in its own file under layers/
// and is included at build time by the host (simple #include resolution).
// Layer calls are guarded by bits in uEnable so any layer can be toggled.

in  vec2 vUV;
out vec4 oCol;

// ── textures ────────────────────────────────────────────────
uniform sampler2D uPrev;    // this field's previous frame
uniform sampler2D uOther;   // other field (for coupling)
uniform sampler2D uCam;     // camera input
uniform vec2      uRes;
uniform float     uTime;
uniform uint      uFrame;
uniform int       uEnable;  // bitfield of enabled layers
uniform int       uFieldId; // 0 = A, 1 = B (for symmetry-breaking)

// ── per-layer parameters ────────────────────────────────────
// warp
uniform float uZoom, uTheta, uPivotX, uPivotY, uTransX, uTransY;
// optics
uniform float uChroma, uBlurX, uBlurY, uBlurAngle;
// gamma
uniform float uGamma;
// color
uniform float uHueRate, uSatGain;
// contrast
uniform float uContrast;
// decay
uniform float uDecay;
// noise
uniform float uNoise;
// couple
uniform float uCouple;
// external (camera)
uniform float uExternal;
// inject
uniform float uInject;
uniform int   uPattern;

// ── layer sources (resolved by host before compile) ─────────
#include "common.glsl"
#include "layers/warp.glsl"
#include "layers/optics.glsl"
#include "layers/gamma.glsl"
#include "layers/color.glsl"
#include "layers/contrast.glsl"
#include "layers/decay.glsl"
#include "layers/noise.glsl"
#include "layers/couple.glsl"
#include "layers/external.glsl"
#include "layers/inject.glsl"

// ── layer enable bits (mirror the host enum) ────────────────
const int L_WARP     = 1<<0;
const int L_OPTICS   = 1<<1;
const int L_GAMMA    = 1<<2;
const int L_COLOR    = 1<<3;
const int L_CONTRAST = 1<<4;
const int L_DECAY    = 1<<5;
const int L_NOISE    = 1<<6;
const int L_COUPLE   = 1<<7;
const int L_EXTERNAL = 1<<8;
const int L_INJECT   = 1<<9;

// ── the pipeline ────────────────────────────────────────────
// Order matters. It corresponds (roughly) to the physical signal path:
// camera optics → analog electronics → mixing → sensor noise → triggering.
void main() {
    vec2 uv     = vUV;
    vec2 src_uv = uv;

    //  1. geometric warp (modifies the sample location)
    if ((uEnable & L_WARP) != 0) src_uv = warp_apply(uv);

    //  2. optics: anisotropic blur + chromatic aberration at sample
    vec4 col;
    if ((uEnable & L_OPTICS) != 0) col = optics_sample(uPrev, src_uv);
    else                            col = texture(uPrev, src_uv);

    //  3a. gamma in — linearise for the "analog" stages
    if ((uEnable & L_GAMMA) != 0) col = gamma_in_apply(col);

    //  4. colour: HSV hue rotation + saturation
    if ((uEnable & L_COLOR) != 0) col = color_apply(col);

    //  5. nonlinear response curve (S-curve)
    if ((uEnable & L_CONTRAST) != 0) col = contrast_apply(col);

    //  3b. gamma out — re-encode before mixing with display-gamma signals
    if ((uEnable & L_GAMMA) != 0) col = gamma_out_apply(col);

    //  7. couple: blend in the other field (Kaneko)
    if ((uEnable & L_COUPLE) != 0) col = couple_apply(col, uv);

    //  8. external: blend in camera
    if ((uEnable & L_EXTERNAL) != 0) col = external_apply(col, uv);

    //  6. decay: per-frame bleed
    if ((uEnable & L_DECAY) != 0) col = decay_apply(col);

    //  9. noise: thermal sensor floor
    if ((uEnable & L_NOISE) != 0) col = noise_apply(col, uv, uTime, uFrame);

    // 10. inject: triggered pattern perturbation
    if ((uEnable & L_INJECT) != 0) col = inject_apply(col, uv);

    oCol = vec4(col.rgb, 1.0);
}
