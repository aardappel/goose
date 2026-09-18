// Screen-space squares along the bottom, one per instance, which also picks
// the icon's layer.
layout(location = 0) in vec2 a_corner;
layout(set = 1, binding = 0) uniform Icons { vec4 place; };   // x, y of the first; size; spacing
layout(location = 0) out vec3 v_uvw;
void main() {
    vec2 p = place.xy + vec2(float(gl_InstanceIndex) * place.w, 0.0) + a_corner * place.z;
    v_uvw = vec3(a_corner.x, 1.0 - a_corner.y, float(gl_InstanceIndex));
    gl_Position = vec4(p, 0.0, 1.0);
}
