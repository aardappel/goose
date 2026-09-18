// The shader is compiled with the program, so what names it is a literal, or
// a global holding one: not a local variable.
fn main() {
    let name = "probe.frag";
    let fs = embed_shader(name);
    print(fs.len);
}
// error: embed_shader takes string literals, and let or const globals initialized with one
