// Samples a storage-written texture with a graphics shader.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 result;
layout(set = 2, binding = 0) uniform sampler2D board;
void main() { result = texture(board, v_uv); }
