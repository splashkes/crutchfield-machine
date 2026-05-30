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

// Classify the loop. Thresholds picked to match the cockpit's regime
// bar and the snapshot-tagger; keep them aligned with the table in
// docs/features/DYNAMICS.md if you change them.
//
//   STABLE     ρ < 0.998 AND K_c < 0.3
//   TURBULENT  K_c >= 0.3
//   CHAOTIC    K_c >= 0.6
//   MARGINAL   ρ >= 0.998
//   DIVERGENT  ρ > 1.001
inline int classify_regime(float rho, float Kc) {
    if (rho > 1.001f) return DIVERGENT;
    if (rho > 0.998f) return MARGINAL;
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
