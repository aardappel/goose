// gfx_compute with the ngfx module, headless: buffers read and written by a
// compute shader through pointers in its root, read back as floats; a
// storage texture and an atomic counter written by one, the texture then
// sampled by a fragment shader and read by another compute shader; and a
// buffer updated in part and read back. Every dispatch's root carries what
// gfx_compute bound and pushed as uniforms, and the one hazard ngfx cannot
// see -- shaders reading what a dispatch wrote -- is the program's barrier.
import std;
import vec;
import os;
import ngfx;

// The roots, as the shaders declare them.
struct Scale { xs: u64, ys: u64, k: f32, count: u32 }
struct Pattern { whites: u64, board: u32 }
struct SumRows { sums: u64, board: u32 }
struct Sample { board: u32, sampler: u32 }

fn open_or_skip() -> bool {
    if ngfx::open_headless(16, 16) { return true; }
    write_stderr(str("ngfx: ", ngfx::error(), "\n"));
    return false;
}

fn main() {
    guard open_or_skip() else { return; }

    // ys[i] = xs[i] * k + i over 200 of a 256-float buffer, in groups of 64.
    let scale = embed_slang("""
        #include <NoGraphicsAPI/shader.slang>
        struct Scale { float* xs; float* ys; float k; uint count; };
        GPU_ROOT(Scale, root);
        [shader("compute")] [numthreads(64, 1, 1)]
        void scale(uint3 id : SV_DispatchThreadID) {
            uint i = id.x;
            if (i < root.count) root.ys[i] = root.xs[i] * root.k + float(i);
        }
        """);
    var xs: f32[>..] = [];
    for i in 200 { xs.push(i * 0.5); }
    let input = ngfx::buffer(bytes_of(xs));
    let output = ngfx::buffer(256 * 4, "");
    ngfx::bind(ngfx::compute_pipeline(scale));
    ngfx::dispatch(ngfx::root(Scale { xs: ngfx::address(input), ys: ngfx::address(output), k: 4.0,
                                      count: 200 }), 4, 1, 1);
    var ys: f32[>..] = [];
    ngfx::read_buffer(output, ys, 256);
    var right = 0;
    for i in 200 {
        if ys[i] == xs[i] * 4.0 + i { right++; }
    }
    print("scale: ", right, " of 200 right, last ", ys[199], ", past the count ", ys[200], " ",
          ys[255], ", buffer of ", ngfx::buffer_size(output), " bytes");

    // A checkerboard of 4 x 4 cells into a storage texture, counting its
    // white texels through an atomic counter.
    let pattern = embed_slang("""
        #include <NoGraphicsAPI/shader.slang>
        struct Pattern { uint* whites; uint board; };
        GPU_ROOT(Pattern, root);
        [shader("compute")] [numthreads(8, 8, 1)]
        void pattern(uint3 id : SV_DispatchThreadID) {
            RWTexture2D<float4> board = gpu_texture<RWTexture2D<float4>>(root.board);
            int2 p = int2(id.xy);
            bool white = ((p.x / 4) + (p.y / 4)) % 2 == 0;
            board[p] = white ? float4(1.0) : float4(0.0, 0.0, 0.0, 1.0);
            if (white) InterlockedAdd(root.whites[0], 1u);
        }
        """);
    let board = ngfx::texture2d(16, 16, ngfx::RGBA8, ngfx::STORAGE | ngfx::SAMPLED);
    let counter = ngfx::buffer(4, "");
    ngfx::bind(ngfx::compute_pipeline(pattern));
    ngfx::dispatch(ngfx::root(Pattern { whites: ngfx::address(counter),
                                        board: ngfx::storage_index(board) }), 2, 2, 1);
    var whites: u32[>..] = [];
    ngfx::read_buffer(counter, whites, 1);
    // The board is sampled by a pass and read by another dispatch below.
    ngfx::barrier(ngfx::COMPUTE_WRITE, ngfx::FRAGMENT_READ | ngfx::COMPUTE_READ);
    let px = ngfx::read_pixels(board);
    var row0: u8[>..] = [];
    for x in 16 { row0.push(if px[x * 4] == 255 { 'W' } else { '.' }); }
    print("pattern: ", whites[0], " white texels, row 0 ", row0);

    // The board sampled by a fragment shader, texel for texel, over one
    // triangle covering the target.
    let sample_board = embed_slang("""
        #include <NoGraphicsAPI/shader.slang>
        struct Sample { uint board; uint sampler; };
        GPU_ROOT(Sample, root);
        struct Varyings { float4 position : SV_Position; float2 uv : TEXCOORD0; };
        [shader("vertex")]
        Varyings fullVertex(uint vertex_id : SV_VertexID) {
            float2 p = float2(float((vertex_id << 1) & 2), float(vertex_id & 2));
            Varyings o;
            o.uv = p;
            o.position = float4(p * 2.0 - 1.0, 0.0, 1.0);
            return o;
        }
        [shader("fragment")]
        float4 sampleBoard(Varyings i) : SV_Target0 {
            Texture2D<float4> board = gpu_texture<Texture2D<float4>>(root.board);
            return board.Sample(gpu_sampler(root.sampler), i.uv);
        }
        """);
    let nearest = ngfx::sampler(ngfx::NEAREST, ngfx::CLAMP);
    let copy = ngfx::render_target(16, 16, ngfx::RGBA8);
    ngfx::begin_pass(copy, ngfx::Texture { 0 }, float4 { 0.5, 0.5, 0.5, 1.0 });
    ngfx::bind(ngfx::pipeline(sample_board));
    ngfx::draw(ngfx::root(Sample { board: ngfx::index(board), sampler: ngfx::index(nearest) }), 3);
    ngfx::end_pass();
    let copied = ngfx::read_pixels(copy);
    var same = copied.len == px.len;
    for i in px.len { same = same && copied[i] == px[i]; }
    print("pattern sampled by a fragment shader as written ", same);

    // A compute shader reading the board, each invocation summing a row's
    // red channel.
    let sum_rows = embed_slang("""
        #include <NoGraphicsAPI/shader.slang>
        struct SumRows { float* sums; uint board; };
        GPU_ROOT(SumRows, root);
        [shader("compute")] [numthreads(1, 1, 1)]
        void sumRows(uint3 id : SV_DispatchThreadID) {
            Texture2D<float4> src = gpu_texture<Texture2D<float4>>(root.board);
            int y = int(id.x);
            float s = 0.0;
            for (int x = 0; x < 16; x++) s += src.Load(int3(x, y, 0)).r;
            root.sums[y] = s;
        }
        """);
    let rows = ngfx::buffer(16 * 4, "");
    ngfx::bind(ngfx::compute_pipeline(sum_rows));
    ngfx::dispatch(ngfx::root(SumRows { sums: ngfx::address(rows), board: ngfx::index(board) }),
                   16, 1, 1);
    var sums: f32[>..] = [];
    ngfx::read_buffer(rows, sums, 16);
    print("row sums: ", sums);

    // Part of a buffer replaced, then read back as u32.
    let patch: u32[4] = [1, 2, 3, 4];
    ngfx::update_buffer(output, 16, bytes_of(patch));
    var words: u32[>..] = [];
    ngfx::read_buffer(output, words, 8);
    print("updated at byte 16: ", words[4..8]);
    ngfx::close();
}
