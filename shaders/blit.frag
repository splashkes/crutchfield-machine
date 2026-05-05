#version 410 core
in  vec2 vUV;
out vec4 oCol;
uniform sampler2D uSrc;
uniform vec2 uRes;
uniform float uTime;
// Display-only brightness multiplier. Intentionally applied in the blit
// (NOT in the feedback write) so scaling doesn't cascade through the
// loop — dynamics stay identical regardless of brightness setting, only
// what lands on the window changes. Recording path reads the sim FBO
// before blit and is therefore also unaffected.
uniform float uBrightness;
uniform int uSphereMode;

vec2 sphere_oct_encode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z) + 1e-6);
    vec2 e = n.xy;
    if (n.z < 0.0) {
        e = (1.0 - abs(e.yx)) * sign(e.xy);
    }
    return e * 0.5 + 0.5;
}

vec3 sphere_rotate_y(vec3 p, float a) {
    float c = cos(a), s = sin(a);
    return vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}

void main() {
    vec4 c;
    if (uSphereMode != 0) {
        float aspect = uRes.x / max(uRes.y, 1.0);
        vec2 p = vUV * 2.0 - 1.0;
        p.x *= aspect;
        float r2 = dot(p, p);
        if (r2 > 0.92 * 0.92) {
            oCol = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        vec3 n = normalize(vec3(p, sqrt(max(0.0, 0.92 * 0.92 - r2))));
        n = sphere_rotate_y(n, uTime * 0.045);
        c = texture(uSrc, sphere_oct_encode(n));
        float light = 0.52 + 0.48 * max(n.z, 0.0);
        c.rgb *= light;
    } else {
        c = texture(uSrc, vUV);
    }
    oCol = vec4(c.rgb * uBrightness, c.a);
}
