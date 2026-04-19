// layers/thermal.glsl
// Heat-shimmer to turbulent-air displacement between camera and monitor.
// Operates by perturbing the UV coordinate used to sample the previous
// frame — so it's an "air between the rig" effect rather than a post filter.
// That means whatever shimmer introduces *also feeds back*, which is where
// the interesting dynamics live.
//
//   uThermAmp   : displacement amplitude (0 = off, 0.01 = barely shimmer,
//                 0.05 = obvious heat, 0.15 = heavy distortion, 0.3+ = chaos)
//   uThermScale : spatial scale of the noise field (1 = very wide rolls,
//                 8 = fine grain, 20+ = dense speckle)
//   uThermSpeed : how fast the noise evolves in time (1 = slow drift,
//                 5 = active shimmer, 15 = turbulent)
//   uThermRise  : vertical bias — makes noise phase advect upward, so the
//                 distortion "rises" like hot air (0 = isotropic, 1 = strong)
//   uThermSwirl : rotational bias — adds a curl component so displacement
//                 has vorticity. 0 = pure translation-like, 1+ = tornado-y
//
// The noise is 2 octaves of value noise (cheap on 3090). The displacement
// is computed as the gradient of a scalar field, which guarantees it's
// divergence-free (incompressible) when swirl is maxed — a tiny nod to
// real fluid behaviour without doing any actual CFD.

// 2D value noise — reuses the hash21 defined in common.glsl.
float vnoise2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i,               0.0);
    float b = hash21(i + vec2(1,0),   0.0);
    float c = hash21(i + vec2(0,1),   0.0);
    float d = hash21(i + vec2(1,1),   0.0);
    vec2 u = f * f * (3.0 - 2.0 * f);   // smoothstep
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Sample a scalar "temperature field" at (uv, time). Two octaves is enough
// for visual richness without killing performance.
float thermal_field(vec2 uv, float t) {
    // Rising bias: subtract velocity*time from the y coordinate so the
    // field appears to travel upward.
    vec2 q = uv;
    q.y -= t * uThermRise * 0.20;

    float v  =       vnoise2(q * uThermScale        + vec2(t * uThermSpeed * 0.13, 0.0));
    v += 0.5 * vnoise2(q * uThermScale * 2.1 + vec2(0.0, t * uThermSpeed * 0.19));
    return v / 1.5;
}

// Offset the sample UV by the thermal field. We compute the gradient of
// the scalar field by central differences, then optionally rotate it 90°
// to get a curl (divergence-free) component for swirl/tornado behaviour.
vec2 thermal_warp(vec2 uv) {
    if (uThermAmp < 0.0001) return uv;
    float t = uTime;
    float eps = 1.0 / max(uRes.x, uRes.y);
    float n_px = thermal_field(uv + vec2(eps, 0.0), t);
    float n_mx = thermal_field(uv - vec2(eps, 0.0), t);
    float n_py = thermal_field(uv + vec2(0.0, eps), t);
    float n_my = thermal_field(uv - vec2(0.0, eps), t);

    // Gradient → translational-looking motion.
    vec2 grad  = vec2(n_px - n_mx, n_py - n_my) / (2.0 * eps);
    // Curl = rotate gradient 90° → divergence-free, vortex-looking motion.
    vec2 curl  = vec2(-grad.y, grad.x);

    // Mix between pure gradient and pure curl via swirl. Swirl of 0 gives
    // translation-ish heat-shimmer; swirl of 1 gives pure vorticity; above
    // 1 exaggerates the rotational component. Keep swirl in [0, 2] typ.
    vec2 disp = mix(grad, curl, clamp(uThermSwirl, 0.0, 1.0));
    if (uThermSwirl > 1.0) disp += (uThermSwirl - 1.0) * curl;

    return uv + disp * uThermAmp;
}
