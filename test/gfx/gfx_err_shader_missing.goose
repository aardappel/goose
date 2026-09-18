// The shader file is resolved relative to the file calling embed_shader.
fn main() {
    let fs = embed_shader("no_such_shader.frag");
    print(fs.len);
}
// error: cannot open shader file
