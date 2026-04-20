// layers/contrast.metal — S-curve around mid-grey.

static float4 contrast_apply(float4 c, constant Uniforms& U) {
    return float4(clamp((c.rgb - 0.5) * U.contrast + 0.5, 0.0, 1.0), c.a);
}
