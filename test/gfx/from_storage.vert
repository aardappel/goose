// Positions from a storage buffer by vertex index, with no vertex buffer.
layout(std430, set = 0, binding = 0) readonly buffer Points { vec4 points[]; };
layout(location = 0) out vec4 v_color;
void main() {
    vec4 p = points[gl_VertexIndex];
    v_color = vec4(1.0, 1.0, 0.0, 1.0);
    gl_Position = vec4(p.xy, 0.0, 1.0);
}
