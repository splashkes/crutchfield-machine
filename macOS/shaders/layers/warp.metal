// layers/warp.metal — see GL version for explanation. Field B inverts theta.

static float2 warp_apply(float2 uv, constant Uniforms& U) {
    float theta = (U.fieldId == 0) ? U.theta : -U.theta;
    float2 pivot = float2(U.pivotX, U.pivotY);
    float2 p = uv - pivot;
    float cs = cos(theta), sn = sin(theta);
    p = float2(cs*p.x - sn*p.y, sn*p.x + cs*p.y);
    p = p / U.zoom + pivot;
    p += float2(U.transX, U.transY);
    return p;
}
