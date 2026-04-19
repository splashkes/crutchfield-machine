// layers/warp.glsl
// Geometric transform. Fields 0,2 use +theta; fields 1,3 use -theta, so the
// ring-coupled setup produces four non-degenerate dynamics.

vec2 warp_apply(vec2 uv) {
    float sgn = (uFieldId == 0 || uFieldId == 2) ? 1.0 : -1.0;
    float theta = sgn * uTheta;

    vec2 pivot = vec2(uPivotX, uPivotY);
    vec2 p = uv - pivot;
    float cs = cos(theta), sn = sin(theta);
    p = vec2(cs*p.x - sn*p.y, sn*p.x + cs*p.y);
    p = p / uZoom + pivot;
    p += vec2(uTransX, uTransY);
    return p;
}
