#version 460 core
in  vec2 vUV;
out vec4 oCol;
uniform sampler2D uSrc;
void main() { oCol = texture(uSrc, vUV); }
