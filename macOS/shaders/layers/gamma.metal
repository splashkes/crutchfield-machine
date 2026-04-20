// layers/gamma.metal — linearise before processing, re-encode after.

static float4 gamma_in_apply(float4 c, constant Uniforms& U) {
    return float4(pow(max(c.rgb, float3(0.0)), float3(U.gamma)), c.a);
}

static float4 gamma_out_apply(float4 c, constant Uniforms& U) {
    return float4(pow(max(c.rgb, float3(0.0)), float3(1.0 / U.gamma)), c.a);
}
