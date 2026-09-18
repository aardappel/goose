// Drawing with the gfx module, headless: vertex and index buffers, uniform
// blocks, the depth test, instancing from a second vertex buffer in a
// compact format, positions read from a storage buffer, alpha blending, and a
// multisampled target resolved. Everything lies on pixel boundaries, so every
// backend and GPU produces the same pixels, which are read back and printed.
import std;
import vec;
import os;
import gfx;

let W = 64;

// The uniform blocks of the rectangle shaders in main. std140 rounds a block
// to a multiple of 16 bytes, where a Goose struct is packed: hence the pad.
struct Place { rect: float4, depth: f32, pad 12 }
struct Paint { color: float4 }

struct Instance { offset: float2, color: u8[4] }

fn open_or_skip() -> bool {
    if gfx::open_headless(W, W) { return true; }
    write_stderr(str("gfx: ", gfx::error(), "\n"));
    return false;
}

fn pixel(px: u8[:], x: i64, y: i64) -> u8[>..] {
    let i = (y * W + x) * 4;
    str(px[i], ",", px[i + 1], ",", px[i + 2], ",", px[i + 3])
}

fn count(px: u8[:], r: u8, g: u8, b: u8) -> i64 {
    var n = 0;
    for i in W * W {
        if px[i * 4] == r && px[i * 4 + 1] == g && px[i * 4 + 2] == b { n++; }
    }
    return n;
}

// A rectangle from (x0, y0) to (x1, y1) in clip space, with the rectangle
// shaders bound, and the unit square with its indices.
fn rect(x0: f32, y0: f32, x1: f32, y1: f32, depth: f32, color: float4) {
    gfx::uniforms(gfx::VERTEX, 0, Place { rect: float4 { x0, y0, x1, y1 }, depth: depth });
    gfx::uniforms(gfx::FRAGMENT, 0, Paint { color: color });
    gfx::draw_indexed(6);
}

fn main() {
    guard open_or_skip() else { return; }
    let corners: float2[4] = [float2 { 0.0, 0.0 }, float2 { 1.0, 0.0 }, float2 { 1.0, 1.0 },
                              float2 { 0.0, 1.0 }];
    let indices: u16[6] = [0, 1, 2, 0, 2, 3];
    let unit = gfx::vertex_buffer(bytes_of(corners));
    let idx = gfx::index_buffer(bytes_of(indices));
    // The rectangle shaders: the vertex buffer holds the unit square, placed
    // by a uniform block (a rectangle in clip space, and a depth) and painted
    // by another.
    let quad_vs = embed_shader("vert", """
        layout(location = 0) in vec2 a_pos;
        layout(set = 1, binding = 0) uniform Place { vec4 rect; float depth; };
        void main() {
            vec2 p = mix(rect.xy, rect.zw, a_pos);
            gl_Position = vec4(p, depth, 1.0);
        }
        """);
    let solid_fs = embed_shader("frag", """
        layout(location = 0) out vec4 result;
        layout(set = 3, binding = 0) uniform Paint { vec4 color; };
        void main() { result = color; }
        """);
    // The color the vertex shader gives.
    let varying_fs = embed_shader("frag", """
        layout(location = 0) in vec4 v_color;
        layout(location = 0) out vec4 result;
        void main() { result = v_color; }
        """);

    // The depth test: the top half nearest, the left half behind it, and the
    // whole screen furthest, drawn last, showing only at the bottom right.
    let depth = gfx::pipeline(quad_vs, solid_fs, gfx::PipelineDesc { depth_test: true,
                                                                    depth_write: true });
    gfx::begin_screen(float4 { 0.0, 0.0, 0.25, 1.0 });
    gfx::bind(depth);
    gfx::bind_vertex_buffer(unit);
    gfx::bind_index_buffer(idx, 2);
    rect(-1.0, 0.0, 1.0, 1.0, 0.25, float4 { 0.0, 1.0, 0.0, 1.0 });
    rect(-1.0, -1.0, 0.0, 1.0, 0.5, float4 { 1.0, 0.0, 0.0, 1.0 });
    rect(-1.0, -1.0, 1.0, 1.0, 0.75, float4 { 1.0, 1.0, 1.0, 1.0 });
    gfx::end_pass();
    var px = gfx::read_pixels(gfx::screen());
    print("depth: top ", pixel(px, 40, 10), ", bottom left ", pixel(px, 10, 50),
          ", bottom right ", pixel(px, 50, 50));
    print("depth: ", count(px, 0, 255, 0), " green, ", count(px, 255, 0, 0), " red, ",
          count(px, 255, 255, 255), " white");

    // Instancing: four squares from one draw, placed and colored per
    // instance by a second vertex buffer holding its colors as four bytes.
    let instanced_vs = embed_shader("vert", """
        layout(location = 0) in vec2 a_corner;
        layout(location = 1) in vec2 i_offset;
        layout(location = 2) in vec4 i_color;
        layout(location = 0) out vec4 v_color;
        void main() {
            v_color = i_color;
            gl_Position = vec4(i_offset + a_corner * 0.25, 0.0, 1.0);
        }
        """);
    var inst_desc = gfx::PipelineDesc { instance_location: 1 };
    inst_desc.formats[2] = gfx::VERTEX_UBYTE4_NORM;
    let inst = gfx::pipeline(instanced_vs, varying_fs, inst_desc);
    let placed: Instance[4] = [
        Instance { offset: float2 { -1.0, 0.75 }, color: [255, 0, 0, 255] },
        Instance { offset: float2 { 0.75, 0.75 }, color: [0, 255, 0, 255] },
        Instance { offset: float2 { -1.0, -1.0 }, color: [0, 0, 255, 255] },
        Instance { offset: float2 { 0.75, -1.0 }, color: [255, 255, 0, 255] },
    ];
    let per_instance = gfx::vertex_buffer(bytes_of(placed));
    gfx::begin_screen(float4 { 0.0, 0.0, 0.0, 1.0 });
    gfx::bind(inst);
    gfx::bind_vertex_buffer(0, unit, 0);
    gfx::bind_vertex_buffer(1, per_instance, 0);
    gfx::bind_index_buffer(idx, 2);
    gfx::draw_indexed_instanced(6, 4);
    gfx::end_pass();
    px = gfx::read_pixels(gfx::screen());
    print("instances: ", pixel(px, 3, 3), " ", pixel(px, 60, 3), " ", pixel(px, 3, 60), " ",
          pixel(px, 60, 60), "; ", count(px, 0, 0, 0), " black");

    // No vertex buffer: positions from a storage buffer, by vertex index,
    // covering the bottom right quarter.
    let points: float4[6] = [
        float4 { 0.0, -1.0, 0.0, 1.0 }, float4 { 1.0, -1.0, 0.0, 1.0 },
        float4 { 1.0, 0.0, 0.0, 1.0 }, float4 { 0.0, -1.0, 0.0, 1.0 },
        float4 { 1.0, 0.0, 0.0, 1.0 }, float4 { 0.0, 0.0, 0.0, 1.0 },
    ];
    let stored = gfx::buffer(gfx::BUFFER_STORAGE, bytes_of(points));
    let from_storage_vs = embed_shader("vert", """
        layout(std430, set = 0, binding = 0) readonly buffer Points { vec4 points[]; };
        layout(location = 0) out vec4 v_color;
        void main() {
            vec4 p = points[gl_VertexIndex];
            v_color = vec4(1.0, 1.0, 0.0, 1.0);
            gl_Position = vec4(p.xy, 0.0, 1.0);
        }
        """);
    let from_storage = gfx::pipeline(from_storage_vs, varying_fs);
    gfx::begin_screen(float4 { 0.0, 0.0, 0.0, 1.0 });
    gfx::bind(from_storage);
    gfx::bind_storage_buffer(gfx::VERTEX, 0, stored);
    gfx::draw(6);
    gfx::end_pass();
    px = gfx::read_pixels(gfx::screen());
    print("storage positions: ", count(px, 255, 255, 0), " yellow, at ", pixel(px, 48, 48));

    // Alpha blending: half-transparent red over blue.
    let blend = gfx::pipeline(quad_vs, solid_fs, gfx::PipelineDesc { blend: gfx::BLEND_ALPHA });
    gfx::begin_screen(float4 { 0.0, 0.0, 1.0, 1.0 });
    gfx::bind(blend);
    gfx::bind_vertex_buffer(unit);
    gfx::bind_index_buffer(idx, 2);
    rect(-1.0, -1.0, 1.0, 1.0, 0.0, float4 { 1.0, 0.0, 0.0, 0.5 });
    gfx::end_pass();
    px = gfx::read_pixels(gfx::screen());
    let mid = (32 * W + 32) * 4;
    print("blend: half red over blue ", abs(px[mid] as i64 - 128) <= 1 && px[mid + 1] == 0 &&
                                        abs(px[mid + 2] as i64 - 128) <= 1 && px[mid + 3] == 255);

    // Multisampling: a triangle into a 4x target, resolved. Pixels along its
    // diagonal are partly covered, which only multisampling can show.
    let msaa = gfx::texture(gfx::TextureDesc { usage: gfx::COLOR_TARGET, width: W as i32,
                                               height: W as i32, samples: 4 });
    let resolved = gfx::render_target(W, W, gfx::RGBA8);
    var pass = gfx::PassDesc {};
    pass.color[0] = msaa;
    pass.resolve[0] = resolved;
    let corner_tri: float4[3] = [float4 { -1.0, -1.0, 0.0, 1.0 }, float4 { 1.0, -1.0, 0.0, 1.0 },
                                 float4 { -1.0, 1.0, 0.0, 1.0 }];
    gfx::update_buffer(stored, bytes_of(corner_tri));
    gfx::begin_pass(pass);
    gfx::bind(from_storage);
    gfx::bind_storage_buffer(gfx::VERTEX, 0, stored);
    gfx::draw(3);
    gfx::end_pass();
    px = gfx::read_pixels(resolved);
    var partial = 0;
    for i in W * W {
        if px[i * 4] > 0 && px[i * 4] < 255 { partial++; }
    }
    print("msaa: inside ", pixel(px, 5, 58), ", outside ", pixel(px, 58, 5),
          ", partly covered pixels seen ", partial >= W / 2);
    gfx::close();
}
