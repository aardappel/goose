layout(location = 0) out vec4 result;
layout(set = 3, binding = 0) uniform Paint { vec4 color; };
void main() { result = color; }
