// The lit meshes seen from the light, for the shadow map: depth only.
#include "scene.glsl"
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(std430, set = 0, binding = 0) readonly buffer Instances { vec4 inst[]; };
layout(set = 1, binding = 1) uniform Draw { vec4 draw; };
void main() {
    int i = (int(draw.x) + gl_InstanceIndex) * 3;
    vec3 world = inst[i].xyz + rotate_q(inst[i + 2], a_pos * inst[i + 1].xyz);
    gl_Position = light_view_proj * vec4(world, 1.0);
}
