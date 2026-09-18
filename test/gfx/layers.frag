// Pixel x of an N x 1 target samples layer x of an array texture.
layout(location = 0) out vec4 result;
layout(set = 2, binding = 0) uniform sampler2DArray layers;
void main() { result = texture(layers, vec3(0.5, 0.5, floor(gl_FragCoord.x))); }
