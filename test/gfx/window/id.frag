// Flat color per draw: an object ID the showcase reads back to see what is
// on screen where.
layout(location = 0) out vec4 result;
layout(set = 3, binding = 0) uniform Id { vec4 id; };
void main() { result = id; }
