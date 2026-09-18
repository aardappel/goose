// With one argument embed_shader takes a shader file's path: source written
// in the program comes after its stage.
fn main() {
    let fs = embed_shader("""
        layout(location = 0) out vec4 result;
        void main() { result = vec4(1.0); }
        """);
    print(fs.len);
}
// error: takes a shader file's path; shader source follows its stage
