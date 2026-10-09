// The Slang file is resolved relative to the file calling embed_slang.
import ngfx;

fn main() {
    let shader = embed_slang("no_such_module.slang");
    print(shader.len);
}
// error: cannot open Slang file
