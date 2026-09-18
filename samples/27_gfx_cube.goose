// A window with a spinning, lit cube, drawn with the gfx module
// (docs/stdlib.md): SDL3's GPU API behind a small Goose interface.
//
// The shaders are GLSL, in gfx_cube.vert and gfx_cube.frag beside this file.
// embed_shader compiles each one when this program is compiled -- to SPIR-V
// for Vulkan, MSL for Metal and HLSL for Direct3D 12, all at once -- so the
// same program runs on whichever the machine has, with nothing to load at
// run time.
//
// The arrow keys turn the cube, and Escape or closing the window quits.
// `-- --frames N` stops after N frames. Either way the last frame is saved
// as gfx_cube.png.
//
//   goose 27_gfx_cube.goose
//
// Built from the generated C, the program links the gfx layer and SDL, whose
// paths `goose --gfx-link` prints as a response file:
//   goose -o gfx_cube.c 27_gfx_cube.goose
//   cl gfx_cube.c @<goose --gfx-link msvc>          (or clang-cl)
//   cc gfx_cube.c -o gfx_cube @<goose --gfx-link cc>

import std;
import vec;
import math;
import os;
import gfx;

// One corner of one face: where it is, which way the face points, and the
// face's color as four bytes, which the pipeline below has the shader read
// as a vec4 from 0 to 1.
struct Vertex { pos: float3, normal: float3, color: u8[4] }

// The vertex shader's uniform block: two column-major matrices. std140 lays
// those out with no padding, so the packed Goose struct matches it exactly.
struct Transform { model: gfx::mat4, view_proj: gfx::mat4 }

// Four corners and two triangles per face, counterclockwise seen from
// outside, which is what culling back faces keeps.
fn cube(verts: Vertex[>..]&, indices: u16[>..]&) {
    let axes: float3[3] = [float3 { 1.0, 0.0, 0.0 }, float3 { 0.0, 1.0, 0.0 },
                           float3 { 0.0, 0.0, 1.0 }];
    let colors: u8[4][6] = [[230, 80, 60, 255], [240, 170, 50, 255], [90, 200, 90, 255],
                            [70, 160, 230, 255], [160, 100, 220, 255], [230, 230, 230, 255]];
    for f in 6 {
        // A face's normal, and two directions along it whose cross product
        // is the normal.
        let sign: f32 = if f % 2 == 0 { 1.0 } else { -1.0 };
        let n = axes[f / 2] * float3 { sign, sign, sign };
        let u = axes[(f / 2 + 1) % 3] * float3 { sign, sign, sign };
        let v = cross(n, u);
        let base = verts.len as u16;
        let corners: float2[4] = [float2 { -1.0, -1.0 }, float2 { 1.0, -1.0 },
                                  float2 { 1.0, 1.0 }, float2 { -1.0, 1.0 }];
        for c in corners {
            let p = n + u * float3 { c.x, c.x, c.x } + v * float3 { c.y, c.y, c.y };
            verts.push(Vertex { pos: p * float3 { 0.5, 0.5, 0.5 }, normal: n, color: colors[f] });
        }
        indices.append([base, base + 1, base + 2, base, base + 2, base + 3]);
    }
}

fn main() {
    var frames = -1;
    let argv = args();
    if argv.len == 3 && argv[1] == "--frames" {
        let n, ok = parse_int(argv[2]);
        if ok { frames = n; }
    }
    guard gfx::open("Goose: a gfx cube", 640, 480, gfx::WINDOW_RESIZABLE) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }

    var verts: Vertex[>..] = [];
    var indices: u16[>..] = [];
    cube(verts, indices);
    let vertex_buffer = gfx::vertex_buffer(bytes_of(verts));
    let index_buffer = gfx::index_buffer(bytes_of(indices));
    // The shader's inputs are read from a vertex buffer in order, laid out as
    // Vertex is; only the color needs saying, since it is bytes and not
    // floats.
    var desc = gfx::PipelineDesc { cull: gfx::CULL_BACK, depth_test: true, depth_write: true };
    desc.formats[2] = gfx::VERTEX_UBYTE4_NORM;
    let cube_pipeline = gfx::pipeline(embed_shader("gfx_cube.vert"),
                                      embed_shader("gfx_cube.frag"), desc);

    var yaw: f32 = 0.6;
    var pitch: f32 = 0.5;
    var drawn = 0;
    while gfx::frame() {
        if gfx::key_pressed("Escape") { break; }
        if gfx::key_down("Left") { yaw -= 0.04; }
        if gfx::key_down("Right") { yaw += 0.04; }
        if gfx::key_down("Up") { pitch -= 0.04; }
        if gfx::key_down("Down") { pitch += 0.04; }
        yaw += 0.01;

        // The screen follows the window's size, and the projection its shape.
        let size = gfx::screen_size();
        let aspect = size.x as f32 / size.y as f32;
        let model = gfx::mul(gfx::rotate(float3 { 1.0, 0.0, 0.0 }, pitch),
                             gfx::rotate(float3 { 0.0, 1.0, 0.0 }, yaw));
        let view = gfx::look_at(float3 { 0.0, 0.4, 3.0 }, float3_0, float3 { 0.0, 1.0, 0.0 });
        let transform = Transform { model: model,
                                    view_proj: gfx::mul(gfx::perspective(0.8, aspect, 0.1, 20.0),
                                                        view) };

        gfx::begin_screen(float4 { 0.1, 0.12, 0.16, 1.0 });
        gfx::bind(cube_pipeline);
        gfx::bind_vertex_buffer(vertex_buffer);
        gfx::bind_index_buffer(index_buffer, 2);
        gfx::uniforms(gfx::VERTEX, 0, transform);
        gfx::draw_indexed(indices.len);
        gfx::end_pass();
        drawn++;
        if drawn == frames { break; }
    }

    // The last frame drawn: saved, and read back to see the cube in it.
    let saved = gfx::screenshot("gfx_cube.png");
    let px = gfx::read_pixels(gfx::screen());
    let size = gfx::screen_size();
    let mid = ((size.y as i64 / 2) * size.x as i64 + size.x as i64 / 2) * 4;
    let background: u8[3] = [26, 31, 41];
    var cube_there = false;
    for i in 3 { cube_there = cube_there || abs(px[mid + i] as i64 - background[i]) > 8; }
    print("drew ", drawn, " frames; cube in the middle: ", cube_there, "; saved gfx_cube.png: ",
          saved);
    gfx::close();
}
