// A module with no entry point is nothing a pipeline can be made from.
import ngfx;

fn main() {
    let shader = embed_slang("""
        float twice(float x) { return x * 2.0; }
        """);
    print(shader.len);
}
// error: the module has no entry points
