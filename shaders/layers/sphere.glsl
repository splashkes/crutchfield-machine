// layers/sphere.glsl
// Prototype spherical feedback topology. The 2D feedback texture is treated
// as an octahedral atlas of directions on S^2. Sampling moves along tangent
// directions on the decoded sphere, then re-encodes to the atlas, so the
// feedback field crosses atlas folds as a sphere surface rather than as a
// flat wrapped rectangle.

uniform int   uSphereMode;
uniform float uSphereReverb;

vec2 sphere_oct_encode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z) + 1e-6);
    vec2 e = n.xy;
    if (n.z < 0.0) {
        e = (1.0 - abs(e.yx)) * sign(e.xy);
    }
    return e * 0.5 + 0.5;
}

vec3 sphere_oct_decode(vec2 uv) {
    vec2 f = uv * 2.0 - 1.0;
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
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

void sphere_basis(vec3 n, out vec3 t, out vec3 b) {
    vec3 up = abs(n.z) < 0.92 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    t = normalize(cross(up, n));
    b = normalize(cross(n, t));
}

vec2 sphere_source_uv(vec2 uv, vec2 planarSrc) {
    vec3 n = sphere_oct_decode(uv);

    // Use the existing warp controls as spherical rotations. Translation
    // becomes tangent drift; zoom becomes a gentle pull toward/away from the
    // front pole instead of planar scale.
    n = sphere_rotate_z(n, -uTheta);
    n = sphere_rotate_y(n, -uTransX * 2.5);
    n = sphere_rotate_x(n,  uTransY * 2.5);

    vec3 t, b;
    sphere_basis(n, t, b);
    vec2 flow = planarSrc - uv;
    flow.x *= uRes.x / max(uRes.y, 1.0);
    n = normalize(n + (flow.x * t + flow.y * b) * 1.8);

    float zoomPull = clamp((uZoom - 1.0) * 5.0, -0.35, 0.35);
    n = normalize(mix(n, vec3(0.0, 0.0, 1.0), zoomPull));
    return sphere_oct_encode(n);
}

vec4 sphere_sample(sampler2D tex, vec2 uv) {
    vec3 n = sphere_oct_decode(uv);
    vec3 t, b;
    sphere_basis(n, t, b);

    float r = clamp(uSphereReverb, 0.0, 1.0) * 0.030;
    vec4 c = texture(tex, uv) * 0.38;
    c += texture(tex, sphere_oct_encode(normalize(n + t * r))) * 0.10;
    c += texture(tex, sphere_oct_encode(normalize(n - t * r))) * 0.10;
    c += texture(tex, sphere_oct_encode(normalize(n + b * r))) * 0.10;
    c += texture(tex, sphere_oct_encode(normalize(n - b * r))) * 0.10;
    c += texture(tex, sphere_oct_encode(normalize(n + (t + b) * (r * 0.7071)))) * 0.055;
    c += texture(tex, sphere_oct_encode(normalize(n + (t - b) * (r * 0.7071)))) * 0.055;
    c += texture(tex, sphere_oct_encode(normalize(n + (-t + b) * (r * 0.7071)))) * 0.055;
    c += texture(tex, sphere_oct_encode(normalize(n + (-t - b) * (r * 0.7071)))) * 0.055;
    return c;
}
