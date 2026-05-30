// dynamics.h — derived quantities + regime classification for the
// Crutchfield iterated-map feedback engine.
//
// Single source of truth for the math the DYNAMICS cockpit displays
// AND the math the engine uses to tag snapshots and drive FAILSAFE.
// Both call into the same functions here so the two surfaces can't
// drift independently.
//
// Reference: Crutchfield, J.P. (1984), "Space-time dynamics in video
// feedback", Physica D 10, 229-245. The ρ approximation here is a
// linear stability proxy valid near zoom ≈ 1, theta ≈ 0; it does not
// capture spiral or burst regimes that the rotation matrix introduces.
// See research/PHILOSOPHY.md and CREDITS.md for the symbol mapping.

#pragma once
#include <cmath>

namespace dyn {

// Regime codes. Order matters — drawMathPanel and snapshot tagging both
// switch on these integers.
enum Regime {
    STABLE     = 0,   // fixed point
    TURBULENT  = 1,   // limit cycle / quasi-attractor
    CHAOTIC    = 2,   // chaotic attractor
    MARGINAL   = 3,   // bifurcation edge (noise-modulated)
    DIVERGENT  = 4,   // attractor at infinity
};

// Spectral radius estimate. Linear approximation of the iterated map's
// dominant eigenvalue near zoom ≈ 1, theta ≈ 0. Higher = more memory,
// closer to instability. < 1 decays, ≈ 1 marginal, > 1 divergent.
inline float compute_rho(float decay, float blurX, float blurY) {
    return decay * (1.0f - 0.02f * (blurX + blurY));
}

// Half-life in frames for the storage parameter L (paper's L = decay).
// Returns a large sentinel when decay is at the boundary.
inline float compute_halflife_frames(float decay) {
    if (decay <= 0.0f || decay >= 1.0f) return 1e9f;
    return std::log(0.5f) / std::log(decay);
}

// Diffusion coefficient D from the Gaussian convolution kernel widths.
// Paper's σ_f + σ_v collapsed into a single per-axis σ here.
inline float compute_diffusion(float blurX, float blurY) {
    return 0.5f * (blurX * blurX + blurY * blurY) * 0.5f;
}

// L (paper symbol). The iterated map's storage parameter, expressed in
// the same units as 'decay' for now since the engine doesn't separate
// monitor phosphor from photoconductor leakage. Provided here as a name
// match for paper readers; the cockpit displays both engine and paper
// forms side by side.
inline float compute_L(float decay) { return decay; }

// Symmetry-lock index. The rotation matrix R(φ) in the iterated map
// drives spatial structures toward k-fold symmetry where k ≈ 2π/|φ|.
// Paper shows the 9-fold "symmetry locking" at φ = 40° as the canonical
// example. Returns the nearest integer fold; 0 when φ is too small to
// resolve (no rotation → no lock).
inline int compute_fold_symmetry(float theta) {
    constexpr float TAU = 6.28318530717959f;
    float a = std::fabs(theta);
    if (a < 1e-4f) return 0;
    int n = (int)std::round(TAU / a);
    return (n >= 2 && n <= 200) ? n : 0;
}

// Logarithmic-spiral pitch angle. When zoom ≠ 1 AND θ ≠ 0, points
// circulate outward (or inward) on a log spiral whose pitch is
// atan(ln(b) / φ). Returns radians; 0 when there is no spiral
// (zoom == 1 or θ == 0).
inline float compute_spiral_pitch(float zoom, float theta) {
    if (zoom <= 0.0f || zoom == 1.0f) return 0.0f;
    if (std::fabs(theta) < 1e-4f)     return 0.0f;
    return std::atan(std::log(zoom) / theta);
}

// Classify the loop. Thresholds picked to match the cockpit's regime
// bar and the snapshot-tagger; keep them aligned with the table in
// docs/features/DYNAMICS.md if you change them.
//
//   STABLE     ρ < 0.99  AND K_c < 0.3
//   TURBULENT  K_c >= 0.3
//   CHAOTIC    K_c >= 0.6
//   MARGINAL   ρ >= 0.99  (paper's noise-modulated edge of stability;
//                          0.99 not 0.998 because the ρ proxy carries
//                          a blur-penalty term that caps reachable ρ
//                          near 0.95 at default blur)
//   DIVERGENT  ρ > 1.001
inline int classify_regime(float rho, float Kc) {
    if (rho > 1.001f) return DIVERGENT;
    if (rho > 0.99f)  return MARGINAL;
    if (Kc  > 0.6f)   return CHAOTIC;
    if (Kc  > 0.3f)   return TURBULENT;
    return STABLE;
}

// Operator-friendly regime name. Paper-faithful subtitles live in the
// DYNAMICS panel; for code paths and logs the short name is enough.
inline const char* regime_name(int code) {
    static const char* N[5] = {
        "STABLE", "TURBULENT", "CHAOTIC", "MARGINAL", "DIVERGENT"
    };
    return (code >= 0 && code <= 4) ? N[code] : "?";
}

} // namespace dyn
