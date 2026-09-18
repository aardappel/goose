// Compute with the gfx module, headless: storage buffers read and written by
// a compute shader with a uniform block, read back as floats; a storage
// texture and an atomic counter written by one, the texture then sampled by
// a fragment shader and read by another compute shader; and a buffer
// updated in part and read back.
import std;
import vec;
import os;
import gfx;

// The scaling shader's uniform block: a float and a uint, rounded up to 16
// bytes.
struct Params { k: f32, count: u32, pad 8 }

fn open_or_skip() -> bool {
    if gfx::open_headless(16, 16) { return true; }
    write_stderr(str("gfx: ", gfx::error(), "\n"));
    return false;
}

fn main() {
    guard open_or_skip() else { return; }

    // ys[i] = xs[i] * k + i over 200 of a 256-float buffer, in groups of 64,
    // from a read-only storage buffer into a read-write one.
    let scale = embed_shader("comp", """
        layout(local_size_x = 64) in;
        layout(std430, set = 0, binding = 0) readonly buffer In { float xs[]; };
        layout(std430, set = 1, binding = 0) buffer Out { float ys[]; };
        layout(set = 2, binding = 0) uniform Params { float k; uint count; };
        void main() {
            uint i = gl_GlobalInvocationID.x;
            if (i < count) ys[i] = xs[i] * k + float(i);
        }
        """);
    var xs: f32[>..] = [];
    for i in 200 { xs.push(i as f32 * 0.5); }
    let input = gfx::buffer(gfx::BUFFER_COMPUTE_READ, bytes_of(xs));
    let output = gfx::buffer(gfx::BUFFER_COMPUTE_WRITE, 256 * 4, "");
    let rw: gfx::Buffer[1] = [output];
    gfx::begin_compute(rw);
    gfx::bind(gfx::compute_pipeline(scale));
    gfx::bind_storage_buffer(gfx::COMPUTE, 0, input);
    gfx::uniforms(gfx::COMPUTE, 0, Params { k: 4.0, count: 200 });
    gfx::dispatch(4, 1, 1);
    gfx::end_compute();
    var ys: f32[>..] = [];
    gfx::read_buffer(output, ys, 256);
    var right = 0;
    for i in 200 {
        if ys[i] == xs[i] * 4.0 + i as f32 { right++; }
    }
    print("scale: ", right, " of 200 right, last ", ys[199], ", past the count ", ys[200], " ",
          ys[255], ", buffer of ", gfx::buffer_size(output), " bytes");

    // A checkerboard of 4 x 4 cells into a storage texture, counting its
    // white texels through an atomic counter.
    let pattern = embed_shader("comp", """
        layout(local_size_x = 8, local_size_y = 8) in;
        layout(set = 1, binding = 0, rgba8) uniform writeonly image2D board;
        layout(std430, set = 1, binding = 1) buffer Counter { uint whites[]; };
        void main() {
            ivec2 p = ivec2(gl_GlobalInvocationID.xy);
            bool white = ((p.x / 4) + (p.y / 4)) % 2 == 0;
            imageStore(board, p, white ? vec4(1.0) : vec4(0.0, 0.0, 0.0, 1.0));
            if (white) atomicAdd(whites[0], 1u);
        }
        """);
    let board = gfx::texture2d(16, 16, gfx::RGBA8, gfx::COMPUTE_WRITE | gfx::SAMPLED);
    let counter = gfx::buffer(gfx::BUFFER_COMPUTE_WRITE, 4, "");
    let bufs: gfx::Buffer[1] = [counter];
    let texs: gfx::Texture[1] = [board];
    gfx::begin_compute(bufs, texs);
    gfx::bind(gfx::compute_pipeline(pattern));
    gfx::dispatch(2, 2, 1);
    gfx::end_compute();
    var whites: u32[>..] = [];
    gfx::read_buffer(counter, whites, 1);
    let px = gfx::read_pixels(board);
    var row0: u8[>..] = [];
    for x in 16 { row0.push(if px[x * 4] == 255 { 'W' } else { '.' }); }
    print("pattern: ", whites[0], " white texels, row 0 ", row0);

    // The board sampled by a fragment shader, texel for texel, over one
    // triangle covering the target with no vertex buffer.
    let fullscreen = embed_shader("vert", """
        layout(location = 0) out vec2 v_uv;
        void main() {
            vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
            v_uv = p;
            gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
        }
        """);
    let sample_board = embed_shader("frag", """
        layout(location = 0) in vec2 v_uv;
        layout(location = 0) out vec4 result;
        layout(set = 2, binding = 0) uniform sampler2D board;
        void main() { result = texture(board, v_uv); }
        """);
    let nearest = gfx::sampler(gfx::NEAREST, gfx::CLAMP);
    let copy = gfx::render_target(16, 16, gfx::RGBA8);
    gfx::begin_pass(copy, gfx::Texture { 0 }, float4 { 0.5, 0.5, 0.5, 1.0 });
    gfx::bind(gfx::pipeline(fullscreen, sample_board));
    gfx::bind_texture(gfx::FRAGMENT, 0, board, nearest);
    gfx::draw(3);
    gfx::end_pass();
    let copied = gfx::read_pixels(copy);
    var same = copied.len == px.len;
    for i in px.len { same = same && copied[i] == px[i]; }
    print("pattern sampled by a fragment shader as written ", same);

    // A compute shader reading the board through a sampler, each invocation
    // summing a row's red channel.
    let sum_rows = embed_shader("comp", """
        layout(local_size_x = 1) in;
        layout(set = 0, binding = 0) uniform sampler2D src;
        layout(std430, set = 1, binding = 0) buffer Rows { float sums[]; };
        void main() {
            int y = int(gl_GlobalInvocationID.x);
            float s = 0.0;
            for (int x = 0; x < 16; x++) s += texelFetch(src, ivec2(x, y), 0).r;
            sums[y] = s;
        }
        """);
    let rows = gfx::buffer(gfx::BUFFER_COMPUTE_WRITE, 16 * 4, "");
    let rb: gfx::Buffer[1] = [rows];
    gfx::begin_compute(rb);
    gfx::bind(gfx::compute_pipeline(sum_rows));
    gfx::bind_texture(gfx::COMPUTE, 0, board, nearest);
    gfx::dispatch(16, 1, 1);
    gfx::end_compute();
    var sums: f32[>..] = [];
    gfx::read_buffer(rows, sums, 16);
    print("row sums: ", sums);

    // Part of a buffer replaced, then read back as u32.
    let patch: u32[4] = [1, 2, 3, 4];
    gfx::update_buffer(output, 16, bytes_of(patch));
    var words: u32[>..] = [];
    gfx::read_buffer(output, words, 8);
    print("updated at byte 16: ", words[4..8]);
    gfx::close();
}
