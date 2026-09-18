// The stage comes from the file's extension.
fn main() {
    let fs = embed_shader("probe_common.glsl");
    print(fs.len);
}
// error: the extension must be .vert, .frag or .comp
