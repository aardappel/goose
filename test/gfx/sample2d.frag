layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 result;
layout(set = 2, binding = 0) uniform sampler2D tex;
layout(set = 3, binding = 0) uniform Lod { float lod; };
void main() { result = textureLod(tex, v_uv, lod); }
