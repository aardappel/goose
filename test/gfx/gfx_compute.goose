// Compute with the gfx module, headless: storage buffers read and written by
// a compute shader with a uniform block, read back as floats; a storage
// texture and an atomic counter written by one, the texture then sampled by
// a fragment shader and read by another compute shader; and a buffer
// updated in part and read back.
import std;
import vec;
import os;
import gfx;

// scale.comp's uniform block: a float and a uint, rounded up to 16 bytes.
struct Params { k: f32, count: u32, pad 8 }

fn open_or_skip() -> bool {
    if gfx::open_headless(16, 16) { return true; }
    write_stderr(str("gfx: ", gfx::error(), "\n"));
    return false;
}

fn main() {
    guard open_or_skip() else { return; }

    // ys[i] = xs[i] * k + i over 200 of a 256-float buffer, in groups of 64.
    var xs: f32[>..] = [];
    for i in 200 { xs.push(i as f32 * 0.5); }
    let input = gfx::buffer(gfx::BUFFER_COMPUTE_READ, bytes_of(xs));
    let output = gfx::buffer(gfx::BUFFER_COMPUTE_WRITE, 256 * 4, "");
    let rw: gfx::Buffer[1] = [output];
    gfx::begin_compute(rw);
    gfx::bind(gfx::compute_pipeline(embed_shader("scale.comp")));
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

    // A checkerboard into a storage texture, counting its white texels.
    let board = gfx::texture2d(16, 16, gfx::RGBA8, gfx::COMPUTE_WRITE | gfx::SAMPLED);
    let counter = gfx::buffer(gfx::BUFFER_COMPUTE_WRITE, 4, "");
    let bufs: gfx::Buffer[1] = [counter];
    let texs: gfx::Texture[1] = [board];
    gfx::begin_compute(bufs, texs);
    gfx::bind(gfx::compute_pipeline(embed_shader("pattern.comp")));
    gfx::dispatch(2, 2, 1);
    gfx::end_compute();
    var whites: u32[>..] = [];
    gfx::read_buffer(counter, whites, 1);
    let px = gfx::read_pixels(board);
    var row0: u8[>..] = [];
    for x in 16 { row0.push(if px[x * 4] == 255 { 'W' } else { '.' }); }
    print("pattern: ", whites[0], " white texels, row 0 ", row0);

    // The board sampled by a fragment shader, texel for texel.
    let nearest = gfx::sampler(gfx::NEAREST, gfx::CLAMP);
    let copy = gfx::render_target(16, 16, gfx::RGBA8);
    gfx::begin_pass(copy, gfx::Texture { 0 }, float4 { 0.5, 0.5, 0.5, 1.0 });
    gfx::bind(gfx::pipeline(embed_shader("fullscreen.vert"), embed_shader("board.frag")));
    gfx::bind_texture(gfx::FRAGMENT, 0, board, nearest);
    gfx::draw(3);
    gfx::end_pass();
    let copied = gfx::read_pixels(copy);
    var same = copied.len == px.len;
    for i in px.len { same = same && copied[i] == px[i]; }
    print("pattern sampled by a fragment shader as written ", same);

    // A compute shader reading the board through a sampler, a row sum each.
    let rows = gfx::buffer(gfx::BUFFER_COMPUTE_WRITE, 16 * 4, "");
    let rb: gfx::Buffer[1] = [rows];
    gfx::begin_compute(rb);
    gfx::bind(gfx::compute_pipeline(embed_shader("sum_texture.comp")));
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
