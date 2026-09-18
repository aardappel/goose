// The per-frame uniform block every 3D vertex shader of the showcase reads
// (vertex set 1, binding 0), and the instance transform they share.
layout(set = 1, binding = 0) uniform Scene {
    mat4 view_proj;
    mat4 light_view_proj;
    vec4 eye;
    vec4 light_dir;         // toward the light
    vec4 cam_right;
    vec4 cam_up;
    vec4 params;            // x: time
};

// Rotates v by the unit quaternion q.
vec3 rotate_q(vec4 q, vec3 v) {
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}
