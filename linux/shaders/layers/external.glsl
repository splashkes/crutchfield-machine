// layers/external.glsl
// Camera input in the loop. This is how you get fabric, your face, a laser
// pointer, whatever else you were pointing the physical rig at. The camera
// texture is supplied by the host (V4L2 on Linux, AVFoundation on Mac).
//
//   uExternal : mix fraction (0.0 = pure feedback; 1.0 = pass-through camera)
//
// Y-flipped because most camera pipelines deliver top-down but GL textures
// are bottom-up.

vec4 external_apply(vec4 c, vec2 uv) {
    vec3 cam = texture(uCam, vec2(uv.x, 1.0 - uv.y)).rgb;
    return vec4(mix(c.rgb, cam, uExternal), c.a);
}
