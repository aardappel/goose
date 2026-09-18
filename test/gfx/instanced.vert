// Per vertex a corner of a small square; per instance where it goes and its
// color, which the vertex buffer holds as four bytes.
layout(location = 0) in vec2 a_corner;
layout(location = 1) in vec2 i_offset;
layout(location = 2) in vec4 i_color;
layout(location = 0) out vec4 v_color;
void main() {
    v_color = i_color;
    gl_Position = vec4(i_offset + a_corner * 0.25, 0.0, 1.0);
}
