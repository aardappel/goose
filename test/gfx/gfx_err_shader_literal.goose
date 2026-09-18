// The shader is compiled with the program, so its name is a literal.
fn main() {
    let name = "probe.frag";
    let fs = embed_shader(name);
    print(fs.len);
}
// error: embed_shader takes a string literal
