// An error in Slang a global holds is reported at its line there, naming
// the embed_slang call compiling it.
import ngfx;

let helpers = """
    float twice(float x) { return x * oops; }
    """;

fn main() {
    let shader = embed_slang("#include <NoGraphicsAPI/shader.slang>", helpers, """
        [shader("compute")] [numthreads(1, 1, 1)]
        void fill(uint3 id : SV_DispatchThreadID) { float y = twice(1.0); }
        """);
    print(shader.len);
}
// error: undefined identifier 'oops'. (in the module embedded at
