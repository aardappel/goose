// An error in shader code a global holds is reported at its line there,
// naming the embed_shader call compiling it (run_tests.py checks the line).
let helpers = """
    float twice(float x) { return x * oops; }
    """;

fn main() {
    let fs = embed_shader("frag", helpers, """
        layout(location = 0) out vec4 result;
        void main() { result = vec4(twice(0.5)); }
        """);
    print(fs.len);
}
// error: undeclared identifier 'oops' (in the shader embedded at
