// embed_slang takes a Slang module's file, or its source written in the
// program, in parts if need be: however it is written, one module compiles
// to one blob, with every entry point's name, stage and thread group size.
// Nothing here needs a GPU, only the shader tools.
import std;

// Slang as a global, which embed_slang calls can name.
let doubled_slang = """
    #include <NoGraphicsAPI/shader.slang>
    float doubled(float x) { return x * 2.0; }
    """;
let doubled_too = doubled_slang;

// The blob's magic and its entry points (src/ngfx/ngfx_blob.h): name, stage
// (0 vertex, 1 fragment, 2 compute) and thread group size.
fn u32_at(blob: const u8[:], at: i64) -> i64 {
    var v: i64 = 0;
    for i in 4 { v = v * 256 + blob[at + 3 - i]; }
    v
}
fn describe(what: const u8[:], blob: const u8[:]) {
    var out = str(what, ": ", blob[0..4]);
    for e in u32_at(blob, 8) {
        let at = 32 + e * 72;
        var name: u8[>..] = [];
        for i in 56 {
            if blob[at + i] == 0 { break; }
            name.push(blob[at + i]);
        }
        format(out, ", ", name, " stage ", u32_at(blob, at + 56), " ", u32_at(blob, at + 60), " x ",
               u32_at(blob, at + 64), " x ", u32_at(blob, at + 68));
    }
    print(out);
}

fn main() {
    // probe.slang, which includes probe_common.slang, and the same module
    // written here, including it relative to this file.
    let from_file = embed_slang("probe.slang");
    let written = embed_slang("""
        #include <NoGraphicsAPI/shader.slang>
        #include "probe_common.slang"
        struct Params { float4 tint; float gain; };
        GPU_ROOT(Params, root);
        [shader("fragment")]
        float4 probe() : SV_Target0 { return root.tint * doubled(root.gain); }
        """);
    describe("from a file", from_file);
    describe("written here", written);
    print("written here: the same blob ", written == from_file);

    // The parts of a module are its lines in order: doubled() from a global
    // does what the include did.
    let parts = embed_slang(doubled_slang, """
        struct Params { float4 tint; float gain; };
        GPU_ROOT(Params, root);
        [shader("fragment")]
        float4 probe() : SV_Target0 { return root.tint * doubled(root.gain); }
        """);
    describe("in parts", parts);
    print("in parts: the same blob ", parts == from_file);

    // A global naming another stands for the same text, and a plain string
    // literal among parts is source too.
    let chained = embed_slang(doubled_too,
        "[shader(\"fragment\")] float4 halved() : SV_Target0 { return float4(doubled(0.25)); }");
    describe("through a second global", chained);

    // Every entry point of a module, compute's with its numthreads.
    describe("three stages", embed_slang("""
        #include <NoGraphicsAPI/shader.slang>
        struct Out { uint* values; };
        GPU_ROOT(Out, root);
        [shader("vertex")]
        float4 vs(uint id : SV_VertexID) : SV_Position { return float4(0.0, 0.0, 0.0, 1.0); }
        [shader("fragment")]
        float4 fs() : SV_Target0 { return float4(1.0); }
        [shader("compute")] [numthreads(8, 4, 1)]
        void cs(uint3 id : SV_DispatchThreadID) { root.values[id.x] = id.y; }
        """));
}
