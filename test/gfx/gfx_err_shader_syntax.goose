// A shader that does not compile is a compile error at the embed_shader call,
// reported at the offending line of GLSL in the program (run_tests.py checks
// the line).
import gfx;

fn main() {
    let fs = embed_shader("frag", """
        layout(location = 0) out vec4 result;
        void main() {
            result = vec4(oops);
        }
        """);
    print(fs.len);
}
// error: undeclared identifier 'oops'
