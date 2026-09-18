// A shader that does not compile is a compile error at the embed_shader call,
// carrying the shader's own file and line.
import gfx;

fn main() {
    let fs = embed_shader("gfx_err_syntax.frag");
    print(fs.len);
}
// error: undeclared identifier 'oops'
