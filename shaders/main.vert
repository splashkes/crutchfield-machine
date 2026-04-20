#version 410 core
out vec2 vUV;
void main() {
    vec2 p[3] = vec2[3](vec2(-1.0,-1.0), vec2(3.0,-1.0), vec2(-1.0,3.0));
    gl_Position = vec4(p[gl_VertexID], 0.0, 1.0);
    vUV = (p[gl_VertexID] + 1.0) * 0.5;
}
