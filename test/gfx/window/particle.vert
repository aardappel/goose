// A camera-facing square per spark, six vertices from the vertex index and
// no vertex buffer: positions from the storage buffer the compute pass
// wrote, colors from a 3D noise texture sampled in the vertex shader.
#include "scene.glsl"
layout(set = 0, binding = 0) uniform sampler3D noise;
layout(std430, set = 0, binding = 1) readonly buffer Particles { vec4 parts[]; };
layout(location = 0) out vec2 v_corner;
layout(location = 1) out vec4 v_color;
void main() {
    int corner = gl_VertexIndex % 6;
    vec2 c = vec2(corner == 1 || corner == 2 || corner == 4 ? 1.0 : -1.0,
                  corner == 2 || corner == 4 || corner == 5 ? 1.0 : -1.0);
    vec4 p = parts[gl_InstanceIndex * 2];
    float life = clamp(p.w, 0.0, 1.0);
    vec3 world = p.xyz + (cam_right.xyz * c.x + cam_up.xyz * c.y) * (0.07 * life);
    float n = textureLod(noise, fract(p.xyz * 0.25 + params.x * 0.05), 0.0).r;
    v_corner = c;
    v_color = vec4(1.0, 0.45 + 0.5 * n, 0.15 + 0.3 * n, 1.0) * life;
    gl_Position = view_proj * vec4(world, 1.0);
}
