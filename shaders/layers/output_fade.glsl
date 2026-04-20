// layers/output_fade.glsl
// Bipolar output fade — the V-4's Output Fade dial as a single control.
//   uOutFade in [-1, +1]:
//      -1   → full black
//       0   → pass-through
//      +1   → full white
// Runs absolutely last, after the vfx slots. The faded colour is what
// gets written into the feedback field for next frame, so holding fade
// toward black is also how you wipe the scene to a clean state.

uniform float uOutFade;

vec4 output_fade_apply(vec4 col) {
    if (uOutFade == 0.0) return col;
    vec3 target = (uOutFade > 0.0) ? vec3(1.0) : vec3(0.0);
    return vec4(mix(col.rgb, target, abs(uOutFade)), col.a);
}
