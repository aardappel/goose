// A rectangle per draw: the vertex buffer holds the unit square, placed by
// a uniform block (a rectangle in clip space, and a depth).
layout(location = 0) in vec2 a_pos;
layout(set = 1, binding = 0) uniform Place { vec4 rect; float depth; };
void main() {
    vec2 p = mix(rect.xy, rect.zw, a_pos);
    gl_Position = vec4(p, depth, 1.0);
}
