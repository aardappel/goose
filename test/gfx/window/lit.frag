// Diffuse and specular light from one direction, shadowed by a shadow map
// through hardware depth comparison (four taps), with distance fog.
layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec4 v_shadow;
layout(location = 0) out vec4 result;
layout(set = 2, binding = 0) uniform sampler2D albedo;
layout(set = 2, binding = 1) uniform sampler2DShadow shadow_map;
layout(set = 3, binding = 0) uniform Light {
    vec4 light_dir;
    vec4 light_color;
    vec4 ambient;
    vec4 eye;
    vec4 fog;                // rgb: color, w: density
    vec4 tint;               // multiplies the result; a: its alpha
};
float shadowed(vec4 s) {
    vec3 p = s.xyz / s.w;
    vec2 uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    float texel = 1.0 / 1024.0;
    float lit = 0.0;
    lit += texture(shadow_map, vec3(uv + vec2(-texel, -texel), p.z));
    lit += texture(shadow_map, vec3(uv + vec2(texel, -texel), p.z));
    lit += texture(shadow_map, vec3(uv + vec2(-texel, texel), p.z));
    lit += texture(shadow_map, vec3(uv + vec2(texel, texel), p.z));
    return lit * 0.25;
}
void main() {
    vec3 n = normalize(v_normal);
    vec3 l = normalize(light_dir.xyz);
    vec3 base = texture(albedo, v_uv).rgb * tint.rgb;
    float diffuse = max(dot(n, l), 0.0) * shadowed(v_shadow);
    vec3 view = normalize(eye.xyz - v_world);
    float spec = pow(max(dot(reflect(-l, n), view), 0.0), 32.0) * diffuse;
    vec3 color = base * (ambient.rgb + light_color.rgb * diffuse) + light_color.rgb * spec * 0.4;
    float dist = length(eye.xyz - v_world);
    color = mix(color, fog.rgb, 1.0 - exp(-dist * fog.w));
    result = vec4(color, tint.a);
}
