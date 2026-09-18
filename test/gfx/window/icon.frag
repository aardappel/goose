layout(location = 0) in vec3 v_uvw;
layout(location = 0) out vec4 result;
layout(set = 2, binding = 0) uniform sampler2DArray icons;
void main() { result = texture(icons, v_uvw); }
