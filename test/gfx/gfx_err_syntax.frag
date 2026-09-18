// Deliberately broken: rejected by the shader compiler inside goose.
layout(location = 0) out vec4 result;
void main() {
    result = vec4(oops);
}
