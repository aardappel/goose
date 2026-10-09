// A Slang module that does not compile is a compile error at the
// embed_slang call, reported at the offending line of Slang in the program.
import ngfx;

fn main() {
    let shader = embed_slang("""
        #include <NoGraphicsAPI/shader.slang>
        [shader("compute")] [numthreads(1, 1, 1)]
        void fill(uint3 id : SV_DispatchThreadID) { uint x = oops; }
        """);
    print(shader.len);
}
// error: undefined identifier 'oops'
