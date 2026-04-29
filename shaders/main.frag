#version 410 core
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
uniform int   uBlurQuality;
uniform int   uCAQuality;
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
uniform int   uNoiseQuality;
// Music → visual envelopes (decay on the host). Dropout mode uses these
// to produce per-source glitch flavours.
uniform float uMusKick;
uniform float uMusSnare;
uniform float uMusHat;
uniform float uMusBass;
uniform float uMusOther;
// physics (Crutchfield-faithful camera-side knobs)
uniform int   uInvert;
uniform int   uInvertPeriod;    // apply the invert op every Nth frame
uniform float uSensorGamma;
uniform float uSatKnee;
uniform float uColorCross;
// thermal (air-between-rig turbulence / heat shimmer)
uniform float uThermAmp;
uniform float uThermScale;
uniform float uThermSpeed;
uniform float uThermRise;
uniform float uThermSwirl;
// couple
uniform float uCouple;
// external (camera)
uniform float uExternal;
// dry/wet mix for the feedback effect path
uniform float uFxWet;
uniform float uSourceWet;
// inject
uniform float uInject;
uniform int   uPattern;
uniform float uPatternInject;
uniform float uShapeInject;
uniform int   uShapeKind;
uniform int   uShapeCount;
uniform float uShapeSize;
uniform float uShapeAngle;
// pixelate
uniform int   uPixelateStyle;     // 0 = off; 1..9 = (shape × size)
uniform int   uPixelateBleedIdx;  // 0 off; 1 soft; 2 CRT; 3 melt; 4 fried; 5 burned
uniform int   uPixelateBurnSeed;  // increment to re-roll the burn pattern

// ── layer enable bits (mirror the host enum) ────────────────
// Declared BEFORE the #includes so layer files can reference them —
// pixelate.glsl needs L_WARP / L_THERMAL to know whether to re-apply
// those stages to its cell-centre sample position.
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
const int L_PHYSICS  = 1<<10;
const int L_THERMAL  = 1<<11;

// ── layer sources (resolved by host before compile) ─────────
#include "common.glsl"
#include "layers/warp.glsl"
#include "layers/thermal.glsl"
#include "layers/optics.glsl"
#include "layers/physics.glsl"
#include "layers/gamma.glsl"
#include "layers/color.glsl"
#include "layers/contrast.glsl"
#include "layers/decay.glsl"
#include "layers/noise.glsl"
#include "layers/couple.glsl"
#include "layers/external.glsl"
#include "layers/inject.glsl"
#include "layers/pixelate.glsl"
#include "layers/vfx_slot.glsl"
#include "layers/output_fade.glsl"

// ── the pipeline ────────────────────────────────────────────
// Order matters. It corresponds (roughly) to the physical signal path:
// camera optics → analog electronics → mixing → sensor noise → triggering.
void main() {
    vec2 uv     = vUV;
    vec2 src_uv = uv;
    vec4 dry    = texture(uPrev, uv);

    //  1. geometric warp (modifies the sample location)
    if ((uEnable & L_WARP) != 0) src_uv = warp_apply(uv);

    //  1.5 thermal: air-between-rig shimmer / turbulence. Perturbs the UV
    //  the feedback is sampled from. Because the perturbation enters the
    //  loop, shimmer content feeds back — small amplitude creates visible
    //  heat, larger amplitude creates genuine turbulence.
    if ((uEnable & L_THERMAL) != 0) src_uv = thermal_warp(src_uv);

    // Geometric VFX alter the feedback source before the rest of the stack,
    // so tone/dynamics/noise/coupling can still process the result.
    src_uv = vfx_warp_uv(src_uv, 0);
    src_uv = vfx_warp_uv(src_uv, 1);

    //  2. sample uPrev. When pixelate is on it takes over the sample
    //     (quantizing to a screen-space grid whose cell-centres get
    //     warp+thermal re-applied so pixelated content propagates).
    //     Optics is bypassed in that path — hard blocks don't want
    //     a blur kernel smearing across cell edges.
    vec4 col;
    if (uPixelateStyle != 0)             col = pixelate_apply(uPrev, src_uv, uv);
    else if ((uEnable & L_OPTICS) != 0)  col = optics_sample(uPrev, src_uv);
    else                                  col = texture(uPrev, src_uv);

    // Source/transform dry/wet: crossfade the raw previous image with the
    // warped/thermal/optics sample before downstream tone/dynamics.
    col = mix(dry, col, clamp(uSourceWet, 0.0, 1.0));

    // Controller-held shapes and persistent patterns enter low in the stack
    // so downstream signal stages process them as part of the feedback image.
    if (uPatternInject > 0.0) col = pattern_layer_apply(col, uv);
    if (uShapeInject > 0.0) col = shape_inject_apply(col, uv);
    vec4 fxDry = col;

    //  2.4 invert: always-on (toggled by uInvert itself). Sits outside
    //  the physics layer gate because users turning on "V" in the UI
    //  expect it to work regardless of what else is enabled. Crutchfield's
    //  's' parameter still semantically lives in the physics block; the
    //  inversion is just lifted out so it's always visible.
    //
    //  Applied every `uInvertPeriod` frames (default 20) so the
    //  interaction with feedback produces a slow flip cycle rather than a
    //  60 Hz strobe. uInvertPeriod = 1 restores the legacy every-frame
    //  behaviour.
    int ip = max(uInvertPeriod, 1);
    if (uInvert == 1 && int(uFrame) - (int(uFrame) / ip) * ip == 0) {
        col.rgb = vec3(1.0) - col.rgb;
    }

    //  2.5 physics: Crutchfield's camera-side knobs (sensor gamma, soft
    //  saturation knee, RGB cross-coupling). Placed here because these
    //  all model the photoconductor's *response* to the incoming light —
    //  i.e. they belong inside the camera before any electronic
    //  processing happens (gamma_in, color, contrast).
    if ((uEnable & L_PHYSICS) != 0) col = physics_apply(col);

    //  3a. gamma in — linearise for the "analog" stages
    if ((uEnable & L_GAMMA) != 0) col = gamma_in_apply(col);

    //  4. colour: HSV hue rotation + saturation
    if ((uEnable & L_COLOR) != 0) col = color_apply(col);

    //  5. nonlinear response curve (S-curve)
    if ((uEnable & L_CONTRAST) != 0) col = contrast_apply(col);

    //  3b. gamma out — re-encode before mixing with display-gamma signals
    if ((uEnable & L_GAMMA) != 0) col = gamma_out_apply(col);

    //  6. decay: per-frame bleed. Runs BEFORE mixers so fresh partner-field
    //     and camera content enter at full amplitude against a faded
    //     background — matching an analog rig where the camera sees live
    //     content at scene brightness, not CRT-attenuated brightness.
    //     See development/LAYERS.md §feedback-write stages.
    if ((uEnable & L_DECAY) != 0) col = decay_apply(col);

    //  7. couple: blend in the other field (Kaneko)
    if ((uEnable & L_COUPLE) != 0) col = couple_apply(col, uv);

    //  8. external: blend in camera
    if ((uEnable & L_EXTERNAL) != 0) col = external_apply(col, uv);

    // 10. noise: thermal sensor floor — final additive stage before post.
    if ((uEnable & L_NOISE) != 0) col = noise_apply(col, uv, uTime, uFrame);

    // 11. V-4-style colour/key/temporal effects. Geometric VFX are applied
    //     above as source-UV warps; their late-stage branch intentionally
    //     returns col so they don't discard downstream layer work.
    col = vfx_apply(col, uv, 0);
    col = vfx_apply(col, uv, 1);

    // 11.5 Effect dry/wet: crossfade the raw previous feedback image against
    //      the processed cycle. Triggered inject and output fade stay after
    //      this mixer so performance hits remain decisive.
    col = mix(fxDry, col, clamp(uFxWet, 0.0, 1.0));

    // 12. inject: triggered pattern perturbation
    if ((uEnable & L_INJECT) != 0 || uInject > 0.0) {
        col = inject_apply(col, uv);
    }

    // 13. Output fade — bipolar to black / white. The V-4's Output Fade dial.
    col = output_fade_apply(col);

    oCol = vec4(col.rgb, 1.0);
}
