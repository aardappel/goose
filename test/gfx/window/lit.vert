// A mesh placed by one instance of a storage buffer of three vec4s each:
// position, scale, rotation. The draw's first instance comes from a uniform,
// since SV_InstanceID on Direct3D 12 does not count the draw's own.
#include "scene.glsl"
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(std430, set = 0, binding = 0) readonly buffer Instances { vec4 inst[]; };
layout(set = 1, binding = 1) uniform Draw { vec4 draw; };   // x: first instance, y: uv tiling
layout(location = 0) out vec3 v_world;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_shadow;
void main() {
    int i = (int(draw.x) + gl_InstanceIndex) * 3;
    vec3 pos = inst[i].xyz;
    vec3 scale = inst[i + 1].xyz;
    vec4 rot = inst[i + 2];
    vec3 world = pos + rotate_q(rot, a_pos * scale);
    v_world = world;
    v_normal = normalize(rotate_q(rot, a_normal / scale));
    v_uv = draw.y > 0.0 ? a_uv * draw.y : a_uv;
    v_shadow = light_view_proj * vec4(world, 1.0);
    gl_Position = view_proj * vec4(world, 1.0);
}
