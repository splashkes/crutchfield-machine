// layers/warp.glsl
// Geometric transform. This is "camera pose relative to monitor".
//   uZoom   : scale factor (>1 = frame zooms in each pass → outward spiral)
//   uTheta  : rotation per pass (radians)
//   uPivotX : off-centre zoom pivot X (0.5 = centre)
//   uPivotY : off-centre zoom pivot Y
//   uTransX : post-rotation translation X (per pass, in UV units)
//   uTransY : post-rotation translation Y
//
// Field B runs with negated theta so the two coupled fields are not degenerate.

vec2 warp_apply(vec2 uv) {
    float theta = (uFieldId == 0) ? uTheta : -uTheta;

    vec2 pivot = vec2(uPivotX, uPivotY);
    vec2 p = uv - pivot;
    float cs = cos(theta), sn = sin(theta);
    p = vec2(cs*p.x - sn*p.y, sn*p.x + cs*p.y);
    p = p / uZoom + pivot;
    p += vec2(uTransX, uTransY);
    return p;
}
