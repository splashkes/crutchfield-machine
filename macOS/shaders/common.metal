// common.metal — helpers used by multiple layers.

static float3 rgb2hsv(float3 c) {
    float4 K = float4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
    float4 p = mix(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
    float4 q = mix(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    return float3(abs(q.z + (q.w - q.y) / (6.0*d + 1e-10)),
                  d / (q.x + 1e-10), q.x);
}

static float3 hsv2rgb(float3 c) {
    float4 K = float4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    float3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

static float hash21(float2 p, float t) {
    p = fract(p * float2(443.897, 441.423));
    p += dot(p, p.yx + 19.19 + t);
    return fract(p.x * p.y * (1.0 + t * 0.0001));
}
