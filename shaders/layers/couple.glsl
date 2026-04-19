// layers/couple.glsl
// Kaneko-style coupling with a second feedback field. Field A samples B, and
// B samples A. Because field B runs with mirrored warp/hue (see warp.glsl and
// color.glsl), the two fields produce non-degenerate dynamics even from the
// same initial condition.
//
//   uCouple : mix fraction (0.0 = no coupling; 0.5 = equal blend)
//
// The coupling sample uses the current UV, not the warped src_uv — this is
// the standard CML (coupled map lattice) formulation: local read from the
// neighbour lattice, composed with the local map.

vec4 couple_apply(vec4 c, vec2 uv) {
    vec3 other = texture(uOther, uv).rgb;
    return vec4(mix(c.rgb, other, uCouple), c.a);
}
