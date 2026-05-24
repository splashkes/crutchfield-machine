// layers/clip.glsl
// Video clip layer — composites the active clip onto the feedback stream.
// Runs after the camera (external) layer; same position in the chain.
//
// uClipAmount   : layer mix amount  (0.0 = bypass, 1.0 = full)
// uClipBlendMode: 0=mix  1=add  2=screen  3=luma-key
// uClipActive   : 1 when a clip is loaded and playing

vec4 clip_layer_apply(vec4 c, vec2 uv) {
    if (uClipActive == 0 || uClipAmount <= 0.0) return c;
    vec3 src = texture(uClip, vec2(uv.x, 1.0 - uv.y)).rgb;
    vec3 out_rgb;
    if (uClipBlendMode == 1)      out_rgb = c.rgb + src * uClipAmount;
    else if (uClipBlendMode == 2) out_rgb = mix(c.rgb, 1.0 - (1.0-c.rgb)*(1.0-src), uClipAmount);
    else if (uClipBlendMode == 3) out_rgb = mix(c.rgb, src, uClipAmount * dot(src, vec3(0.2126,0.7152,0.0722)));
    else                          out_rgb = mix(c.rgb, src, uClipAmount);
    return vec4(out_rgb, c.a);
}
