#version 410 core
// Hi-res supersampled blit. Used by the screenshot-hires path to render the
// paused sim FBO into a K-times-larger target, taking N rotated-grid sample
// taps per output pixel. The bilinear texture filter still does the
// reconstruction; we just box-filter over a 4x4 jittered grid within each
// output texel, which gives a closer approximation to a sinc-reconstruction
// than a single bilinear read.

in  vec2 vUV;
out vec4 oCol;
uniform sampler2D uSrc;
uniform float uBrightness;

// 4x4 rotated-grid offsets in [-0.5, 0.5] sub-pixel units of the OUTPUT.
const vec2 OFFS[16] = vec2[16](
    vec2( 0.0625, -0.4375), vec2( 0.4375, -0.0625),
    vec2(-0.0625, -0.3125), vec2( 0.3125,  0.0625),
    vec2(-0.4375, -0.1875), vec2( 0.1875,  0.1875),
    vec2(-0.3125, -0.0625), vec2( 0.0625,  0.3125),
    vec2(-0.1875,  0.0625), vec2( 0.4375,  0.4375),
    vec2(-0.0625,  0.1875), vec2( 0.3125, -0.3125),
    vec2( 0.0625,  0.4375), vec2(-0.4375,  0.1875),
    vec2( 0.1875, -0.1875), vec2(-0.3125,  0.3125)
);

void main() {
    // Output is K-times larger than source. fwidth(vUV) is the size of one
    // output texel in source-UV units. We jitter within that footprint and
    // box-filter over 16 samples per output pixel.
    vec2 dUV = fwidth(vUV);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 16; i++) {
        vec2 uv = vUV + OFFS[i] * dUV;
        sum += texture(uSrc, uv).rgb;
    }
    vec3 c = sum * (1.0 / 16.0);
    oCol = vec4(c * uBrightness, 1.0);
}

