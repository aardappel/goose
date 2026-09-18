// A depth texture shown as gray, sampled plainly rather than compared.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 result;
layout(set = 2, binding = 0) uniform sampler2D depth;
void main() {
    float d = texture(depth, v_uv).r;
    float g = clamp((1.0 - d) * 3.0, 0.0, 1.0);
    result = vec4(g, g, g, 1.0);
}
