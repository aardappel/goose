// The sky: a cube map looked up along the view ray through each pixel.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 result;
layout(set = 2, binding = 0) uniform samplerCube sky;
layout(set = 3, binding = 0) uniform View {
    vec4 right;              // scaled by tan(fov / 2) * aspect
    vec4 up;                 // scaled by tan(fov / 2)
    vec4 forward;
};
void main() {
    vec2 ndc = vec2(v_uv.x * 2.0 - 1.0, 1.0 - v_uv.y * 2.0);
    vec3 dir = normalize(forward.xyz + ndc.x * right.xyz + ndc.y * up.xyz);
    result = texture(sky, dir);
}
