// layers/color.metal — HSV hue rotation + saturation gain. Field B inverts hueRate.

static float4 color_apply(float4 c, constant Uniforms& U) {
    float hueRate = (U.fieldId == 0) ? U.hueRate : -U.hueRate;
    float3 hsv = rgb2hsv(c.rgb);
    hsv.x = fract(hsv.x + hueRate);
    hsv.y = clamp(hsv.y * U.satGain, 0.0, 1.0);
    return float4(hsv2rgb(hsv), c.a);
}
