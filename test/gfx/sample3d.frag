// One slice of a volume, by depth coordinate.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 result;
layout(set = 2, binding = 0) uniform sampler3D volume;
layout(set = 3, binding = 0) uniform Slice { float w; };
void main() { result = texture(volume, vec3(v_uv, w)); }
