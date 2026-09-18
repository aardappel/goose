// The resolved HDR image, tone mapped and gamma corrected, with a vignette.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 result;
layout(set = 2, binding = 0) uniform sampler2D hdr;
layout(set = 3, binding = 0) uniform Post { float exposure; float vignette; float pad0; float pad1; };
void main() {
    vec3 c = texture(hdr, v_uv).rgb * exposure;
    c = c / (1.0 + c);
    c = pow(c, vec3(1.0 / 2.2));
    vec2 d = v_uv - 0.5;
    c *= 1.0 - vignette * dot(d, d);
    result = vec4(c, 1.0);
}
