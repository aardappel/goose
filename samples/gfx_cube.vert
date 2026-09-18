// The cube's vertex shader (27_gfx_cube.goose): each corner moved by the
// model and view-projection matrices of a uniform block, its normal turned
// with the model.
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_color;
layout(set = 1, binding = 0) uniform Transform {
    mat4 model;
    mat4 view_proj;
};
layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec4 v_color;
void main() {
    v_normal = (model * vec4(a_normal, 0.0)).xyz;
    v_color = a_color;
    gl_Position = view_proj * model * vec4(a_pos, 1.0);
}
