// The cube's fragment shader (27_gfx_cube.goose): the face color, lit from
// one direction, with some light everywhere.
layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 result;
void main() {
    float light = max(dot(normalize(v_normal), normalize(vec3(0.4, 0.8, 0.6))), 0.0);
    result = vec4(v_color.rgb * (0.25 + 0.75 * light), 1.0);
}
