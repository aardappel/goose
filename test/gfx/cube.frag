// Pixel x of a 6 x 1 target looks along cube face x's axis: +x, -x, +y, -y,
// +z, -z.
layout(location = 0) out vec4 result;
layout(set = 2, binding = 0) uniform samplerCube sky;
void main() {
    int face = int(gl_FragCoord.x);
    vec3 dir = vec3(1.0, 0.0, 0.0);
    if (face == 1) dir = vec3(-1.0, 0.0, 0.0);
    if (face == 2) dir = vec3(0.0, 1.0, 0.0);
    if (face == 3) dir = vec3(0.0, -1.0, 0.0);
    if (face == 4) dir = vec3(0.0, 0.0, 1.0);
    if (face == 5) dir = vec3(0.0, 0.0, -1.0);
    result = texture(sky, dir);
}
