// 27_gfx_cube on ngfx (docs/stdlib.md): the same window and spinning, lit
// cube, drawn the way NoGraphicsAPI draws. The vertices are plain GPU memory,
// and the shader reads them itself through a pointer in its root, a struct
// this program fills each frame and hands to the draw by address.
//
// The shaders are one Slang module, written in main as a """ string.
// embed_slang compiles it when this program is compiled -- to a metallib for
// Metal and SPIR-V for Vulkan, both at once -- so the program runs on either
// with nothing to load at run time.
//
// The arrow keys turn the cube, and Escape or closing the window quits.
// `-- --frames N` stops after N frames. Either way the last frame is saved
// as ngfx_cube.png.
//
//   goose 27_ngfx_cube.goose
//
// Built from the generated C, the program links the ngfx layer,
// NoGraphicsAPI and SDL, whose paths `goose --ngfx-link` prints as a
// response file:
//   goose -o ngfx_cube.c 27_ngfx_cube.goose
//   cc ngfx_cube.c -o ngfx_cube @<goose --ngfx-link cc>

import std;
import vec;
import math;
import os;
import ngfx;

// One corner of one face: where it is, which way the face points, and the
// face's color as four bytes, which the shader unpacks to a float4 from 0
// to 1. The shader declares the same struct.
struct Vertex { pos: float3, normal: float3, color: u8[4] }

// The draw's root, as the shader declares it: a pointer to the vertices and
// two row-major matrices. C's layout puts the pointer first on its 8-byte
// boundary and the matrices after it with no padding, as Goose packs them.
struct Root { vertices: u64, model: ngfx::float4x4, view_proj: ngfx::float4x4 }

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
    guard ngfx::open("Goose: an ngfx cube", 640, 480, ngfx::WINDOW_RESIZABLE) else {
        write_stderr(str("ngfx: ", ngfx::error(), "\n"));
        return;
    }

    var verts: Vertex[>..] = [];
    var indices: u16[>..] = [];
    cube(verts, indices);
    let vertex_buffer = ngfx::buffer(bytes_of(verts));
    let index_buffer = ngfx::buffer(bytes_of(indices));
    // The vertex shader fetches its corner through the root's pointer, moves
    // it by the model and view-projection matrices, and turns its normal
    // with the model. The fragment shader lights the face's color from one
    // direction, with some light everywhere.
    let cube_shader = embed_slang("""
        #include <NoGraphicsAPI/shader.slang>

        struct Vertex { float3 pos; float3 normal; uint color; };
        struct Root { Vertex* vertices; float4x4 model; float4x4 view_proj; };
        GPU_ROOT(Root, root);

        struct Varyings {
            float4 position : SV_Position;
            float3 normal : NORMAL;
            float4 color : COLOR;
        };

        [shader("vertex")]
        Varyings vertexMain(uint vertex_id : SV_VertexID) {
            Vertex v = root.vertices[vertex_id];
            Varyings o;
            o.normal = mul(root.model, float4(v.normal, 0.0)).xyz;
            o.color = unpackUnorm4x8ToFloat(v.color);
            o.position = mul(root.view_proj, mul(root.model, float4(v.pos, 1.0)));
            return o;
        }

        [shader("fragment")]
        float4 fragmentMain(Varyings i) : SV_Target0 {
            float light = max(dot(normalize(i.normal), normalize(float3(0.4, 0.8, 0.6))), 0.0);
            return float4(i.color.rgb * (0.25 + 0.75 * light), 1.0);
        }
        """);
    let cube_pipeline = ngfx::pipeline(cube_shader, ngfx::PipelineDesc {
        cull: ngfx::CULL_BACK, depth_test: true, depth_write: true });

    var yaw: f32 = 0.6;
    var pitch: f32 = 0.5;
    var drawn = 0;
    while ngfx::frame() {
        if ngfx::key_pressed("Escape") { break; }
        if ngfx::key_down("Left") { yaw -= 0.04; }
        if ngfx::key_down("Right") { yaw += 0.04; }
        if ngfx::key_down("Up") { pitch -= 0.04; }
        if ngfx::key_down("Down") { pitch += 0.04; }
        yaw += 0.01;

        // The screen follows the window's size, and the projection its shape.
        let size = ngfx::screen_size();
        let aspect = size.x as f32 / size.y;
        let model = ngfx::mul(ngfx::rotate(float3 { 1.0, 0.0, 0.0 }, pitch),
                              ngfx::rotate(float3 { 0.0, 1.0, 0.0 }, yaw));
        let view = ngfx::look_at(float3 { 0.0, 0.4, 3.0 }, float3_0, float3 { 0.0, 1.0, 0.0 });

        ngfx::begin_screen(float4 { 0.1, 0.12, 0.16, 1.0 });
        ngfx::bind(cube_pipeline);
        let root = ngfx::root(Root {
            vertices: ngfx::address(vertex_buffer), model: model,
            view_proj: ngfx::mul(ngfx::perspective(0.8, aspect, 0.1, 20.0), view) });
        ngfx::draw_indexed(root, index_buffer, 2, indices.len);
        ngfx::end_pass();
        drawn++;
        if drawn == frames { break; }
    }

    // The last frame drawn: saved, and read back to see the cube in it.
    let saved = ngfx::screenshot("ngfx_cube.png");
    let px = ngfx::read_pixels(ngfx::screen());
    let size = ngfx::screen_size();
    let mid = ((size.y as i64 / 2) * size.x + size.x as i64 / 2) * 4;
    let background: u8[3] = [26, 31, 41];
    var cube_there = false;
    for i in 3 { cube_there = cube_there || abs(px[mid + i] as i64 - background[i]) > 8; }
    print("drew ", drawn, " frames; cube in the middle: ", cube_there, "; saved ngfx_cube.png: ",
          saved);
    ngfx::close();
}
