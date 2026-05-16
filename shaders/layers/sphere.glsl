// layers/sphere.glsl
// True spherical volume mode. The feedback state lives in a ping-pong 3D
// texture. Each feedback pass renders one z-slice; display raymarches the
// volume. The sphere is a boundary condition inside the 3D field, not a 2D
// chart wrapped onto a ball.

uniform int   uSphereMode;
uniform float uSphereReverb;
uniform sampler3D uPrevVol;
uniform sampler3D uOtherVol;
uniform float uVolumeSlice;
uniform float uVolumeSize;

vec3 sphere_volume_pos(vec2 uv) {
    float z = ((uVolumeSlice + 0.5) / max(uVolumeSize, 1.0)) * 2.0 - 1.0;
    return vec3(uv * 2.0 - 1.0, z);
}

vec3 sphere_rotate_x(vec3 p, float a) {
    float c = cos(a), s = sin(a);
    return vec3(p.x, c * p.y - s * p.z, s * p.y + c * p.z);
}

vec3 sphere_rotate_y(vec3 p, float a) {
    float c = cos(a), s = sin(a);
    return vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}

vec3 sphere_rotate_z(vec3 p, float a) {
    float c = cos(a), s = sin(a);
    return vec3(c * p.x - s * p.y, s * p.x + c * p.y, p.z);
}

float sphere_inside_mask(vec3 p) {
    float r = length(p);
    return 1.0 - smoothstep(0.985, 1.035, r);
}

float sphere_boundary_mask(vec3 p) {
    float r = length(p);
    return smoothstep(0.88, 1.02, r);
}

vec3 sphere_volume_uv(vec3 p) {
    return p * 0.5 + 0.5;
}

vec4 sphere_tex(sampler3D tex, vec3 p) {
    float r = length(p);
    if (r > 1.08) return vec4(0.0, 0.0, 0.0, 1.0);

    // Soft radial reflection just past the boundary. This gives waves a
    // rounded wall to push against instead of a hard clipped cube edge.
    if (r > 1.0) {
        vec3 n = p / max(r, 1e-5);
        p = n * (2.0 - r);
    }
    return texture(tex, clamp(sphere_volume_uv(p), vec3(0.0), vec3(1.0)));
}

vec3 sphere_source_pos(vec3 p, vec2 uv, vec2 planarSrc) {
    vec3 q = p;

    // Existing transform controls become true 3D transforms.
    q = sphere_rotate_z(q, -uTheta);
    q = sphere_rotate_y(q, -uTransX * 2.5);
    q = sphere_rotate_x(q,  uTransY * 2.5);

    vec2 flow = planarSrc - uv;
    q.xy += flow * 1.45;

    float zoomScale = clamp(1.0 + (uZoom - 1.0) * 1.8, 0.62, 1.55);
    q /= zoomScale;
    return q;
}

vec4 sphere_sample_volume(sampler3D tex, vec3 p) {
    float r = 0.012 + clamp(uSphereReverb, 0.0, 1.0) * 0.055;
    vec4 c = sphere_tex(tex, p) * 0.34;
    c += sphere_tex(tex, p + vec3( r, 0.0, 0.0)) * 0.075;
    c += sphere_tex(tex, p + vec3(-r, 0.0, 0.0)) * 0.075;
    c += sphere_tex(tex, p + vec3(0.0,  r, 0.0)) * 0.075;
    c += sphere_tex(tex, p + vec3(0.0, -r, 0.0)) * 0.075;
    c += sphere_tex(tex, p + vec3(0.0, 0.0,  r)) * 0.075;
    c += sphere_tex(tex, p + vec3(0.0, 0.0, -r)) * 0.075;

    vec3 d = normalize(vec3(1.0));
    c += sphere_tex(tex, p + d * r) * 0.035;
    c += sphere_tex(tex, p - d * r) * 0.035;
    c += sphere_tex(tex, p + vec3(d.x, -d.y, d.z) * r) * 0.035;
    c += sphere_tex(tex, p - vec3(d.x, -d.y, d.z) * r) * 0.035;
    c += sphere_tex(tex, p + vec3(-d.x, d.y, d.z) * r) * 0.035;
    c += sphere_tex(tex, p - vec3(-d.x, d.y, d.z) * r) * 0.035;
    return c;
}

vec4 sphere_decay_apply(vec4 c, vec3 p) {
    float wall = sphere_boundary_mask(p);
    float wallDecay = mix(1.0, clamp(uBorderDecay, 0.0, 1.0), 0.35);
    float borderMul = mix(1.0, wallDecay, wall);
    float inside = sphere_inside_mask(p);
    return vec4(c.rgb * uDecay * borderMul * inside, c.a);
}

vec4 sphere_couple_apply(vec4 c, vec3 p) {
    vec3 other = sphere_sample_volume(uOtherVol, p).rgb;
    return vec4(clamp(mix(c.rgb, other, uCouple), 0.0, 1.0), c.a);
}

// Legacy atlas names are retained as no-op compatibility for hosts that still
// compile the shared shader but do not enable the volumetric path.
vec2 sphere_source_uv(vec2 uv, vec2 planarSrc) {
    return planarSrc;
}

vec4 sphere_sample(sampler2D tex, vec2 uv) {
    return texture(tex, uv);
}
