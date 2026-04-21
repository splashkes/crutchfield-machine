#version 460 core
in  vec2 vUV;
out vec4 oCol;
uniform sampler2D uSrc;
// Display-only brightness multiplier. Intentionally applied in the blit
// (NOT in the feedback write) so scaling doesn't cascade through the
// loop — dynamics stay identical regardless of brightness setting, only
// what lands on the window changes. Recording path reads the sim FBO
// before blit and is therefore also unaffected.
uniform float uBrightness;
void main() {
    vec4 c = texture(uSrc, vUV);
    oCol = vec4(c.rgb * uBrightness, c.a);
}
