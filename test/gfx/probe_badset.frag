// A sampler outside the set SDL_GPU gives a fragment shader's textures.
layout(location = 0) out vec4 result;
layout(set = 0, binding = 0) uniform sampler2D tex;
void main() { result = texture(tex, vec2(0.5)); }
