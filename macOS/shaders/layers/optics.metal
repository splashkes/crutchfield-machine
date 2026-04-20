// layers/optics.metal — chromatic aberration + anisotropic 5-tap blur.

static float4 optics_sample(texture2d<float> tex, sampler samp,
                            float2 src, constant Uniforms& U) {
    float2 px = 1.0 / U.resolution;
    float bc = cos(U.blurAngle), bs = sin(U.blurAngle);
    float2 bx = float2( bc,  bs) * U.blurX * px;
    float2 by = float2(-bs,  bc) * U.blurY * px;
    float2 rad = (src - float2(0.5));
    float2 ca  = rad * U.chroma;

    float2 off[5] = { float2(0), bx, -bx, by, -by };
    float3 col = float3(0);
    for (int k = 0; k < 5; k++) {
        col.r += tex.sample(samp, src + off[k] - ca).r;
        col.g += tex.sample(samp, src + off[k]     ).g;
        col.b += tex.sample(samp, src + off[k] + ca).b;
    }
    return float4(col / 5.0, 1.0);
}
