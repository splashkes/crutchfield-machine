#version 410 core
in  vec2 vUV;
out vec4 oCol;
uniform sampler2D uSrc;
uniform sampler3D uSrcVol;
uniform vec2 uRes;
uniform float uTime;
// Display-only brightness multiplier. Intentionally applied in the blit
// (NOT in the feedback write) so scaling doesn't cascade through the
// loop — dynamics stay identical regardless of brightness setting, only
// what lands on the window changes. Recording path reads the sim FBO
// before blit and is therefore also unaffected.
uniform float uBrightness;
uniform int uSphereMode;
uniform vec2 uViewRot;

float luma(vec3 c) {
    return dot(max(c, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
}

vec3 sphere_rotate_y(vec3 p, float a) {
    float c = cos(a), s = sin(a);
    return vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}

vec3 sphere_rotate_x(vec3 p, float a) {
    float c = cos(a), s = sin(a);
    return vec3(p.x, c * p.y - s * p.z, s * p.y + c * p.z);
}

vec3 sphere_view_rotate(vec3 p) {
    p = sphere_rotate_x(p, uViewRot.x);
    p = sphere_rotate_y(p, uViewRot.y + uTime * 0.045);
    return p;
}

vec4 sphere_vol_tex(vec3 p) {
    return texture(uSrcVol, clamp(p * 0.5 + 0.5, vec3(0.0), vec3(1.0)));
}

void main() {
    vec4 c;
    if (uSphereMode != 0) {
        float aspect = uRes.x / max(uRes.y, 1.0);
        vec2 p = vUV * 2.0 - 1.0;
        p.x *= aspect;

        float r2 = dot(p, p);
        float baseR = 0.88;
        float feather = 0.06;
        if (r2 > (baseR + feather) * (baseR + feather)) {
            oCol = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }

        float zFront = sqrt(max(0.0, baseR * baseR - r2));
        float zBack = -zFront;
        float edge = 1.0 - smoothstep(baseR - feather, baseR + feather, sqrt(r2));

        vec3 accum = vec3(0.0);
        float alpha = 0.0;
        const int STEPS = 56;
        for (int i = 0; i < STEPS; i++) {
            float t = (float(i) + 0.5) / float(STEPS);
            vec3 q = vec3(p, mix(zFront, zBack, t)) / baseR;
            q = sphere_view_rotate(q);
            vec4 s = sphere_vol_tex(q);
            float density = smoothstep(0.025, 0.95, luma(s.rgb));
            float a = density * 0.055 * (1.0 - alpha);
            float depthLight = 0.72 + 0.28 * (1.0 - t);
            accum += s.rgb * a * depthLight;
            alpha += a;
            if (alpha > 0.985) break;
        }

        // Add a subtle front-surface read so sparse fields still read as a
        // sphere before the volume has filled in.
        vec3 n = normalize(vec3(p, zFront));
        n = sphere_view_rotate(n / baseR);
        vec3 shell = sphere_vol_tex(n).rgb;
        c = vec4((accum + shell * 0.18) * edge, 1.0);
    } else {
        c = texture(uSrc, vUV);
    }
    oCol = vec4(c.rgb * uBrightness, c.a);
}
