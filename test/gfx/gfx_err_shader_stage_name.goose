// Shader source names its stage the way a shader file's extension does.
fn main() {
    let fs = embed_shader("fragment", """
        layout(location = 0) out vec4 result;
        void main() { result = vec4(1.0); }
        """);
    print(fs.len);
}
// error: the stage is "vert", "frag" or "comp", not "fragment"
