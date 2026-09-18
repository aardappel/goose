// A soft round spark, added onto what is there.
layout(location = 0) in vec2 v_corner;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 result;
void main() {
    float d = max(1.0 - dot(v_corner, v_corner), 0.0);
    result = vec4(v_color.rgb * 3.0 * d, d);
}
