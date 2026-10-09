// gfx_render with the ngfx module, headless: indexed draws whose shader
// fetches its corners through the root, roots per draw, the depth test,
// instancing from an array the shader indexes by instance, positions read
// from a buffer, and alpha blending. gfx_render's multisampled target has no
// counterpart: NoGraphicsAPI has no multisampling. Clip space has y down
// here, so the rectangles are gfx_render's with y negated, and the pixels
// read back are the same.
import std;
import vec;
import os;
import ngfx;

let W = 64;

// A rectangle's root, as the shader declares it: the unit square's corners,
// the rectangle in clip space, its depth and its color.
struct Rect { corners: u64, rect: float4, color: float4, depth: f32 }
// The instanced draw's: the corners and the instances.
struct Instanced { corners: u64, instances: u64 }
struct Instance { offset: float2, color: u8[4] }

fn open_or_skip() -> bool {
    if ngfx::open_headless(W, W) { return true; }
    write_stderr(str("ngfx: ", ngfx::error(), "\n"));
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
// shaders bound: the unit square, placed by its root.
fn rect(unit: ngfx::Buffer, idx: ngfx::Buffer, x0: f32, y0: f32, x1: f32, y1: f32, depth: f32,
        color: float4) {
    let root = ngfx::root(Rect { corners: ngfx::address(unit), rect: float4 { x0, y0, x1, y1 },
                                 color: color, depth: depth });
    ngfx::draw_indexed(root, idx, 2, 6);
}

fn main() {
    guard open_or_skip() else { return; }
    let corners: float2[4] = [float2 { 0.0, 0.0 }, float2 { 1.0, 0.0 }, float2 { 1.0, 1.0 },
                              float2 { 0.0, 1.0 }];
    let indices: u16[6] = [0, 1, 2, 0, 2, 3];
    let unit = ngfx::buffer(bytes_of(corners));
    let idx = ngfx::buffer(bytes_of(indices));
    // The rectangle shaders: the unit square's corners, placed and painted
    // by the root.
    let rect_shader = embed_slang("""
        #include <NoGraphicsAPI/shader.slang>
        struct Rect { float2* corners; float4 rect; float4 color; float depth; };
        GPU_ROOT(Rect, root);
        [shader("vertex")]
        float4 rectVertex(uint vertex_id : SV_VertexID) : SV_Position {
            float2 p = lerp(root.rect.xy, root.rect.zw, root.corners[vertex_id]);
            return float4(p, root.depth, 1.0);
        }
        [shader("fragment")]
        float4 rectFragment() : SV_Target0 { return root.color; }
        """);
    let unit_address = ngfx::address(unit);

    // The depth test: the top half nearest, the left half behind it, and the
    // whole screen furthest, drawn last, showing only at the bottom right.
    let depth = ngfx::pipeline(rect_shader, ngfx::PipelineDesc { depth_test: true,
                                                                 depth_write: true });
    ngfx::begin_screen(float4 { 0.0, 0.0, 0.25, 1.0 });
    ngfx::bind(depth);
    rect(unit, idx, -1.0, -1.0, 1.0, 0.0, 0.25, float4 { 0.0, 1.0, 0.0, 1.0 });
    rect(unit, idx, -1.0, -1.0, 0.0, 1.0, 0.5, float4 { 1.0, 0.0, 0.0, 1.0 });
    rect(unit, idx, -1.0, -1.0, 1.0, 1.0, 0.75, float4 { 1.0, 1.0, 1.0, 1.0 });
    ngfx::end_pass();
    var px = ngfx::read_pixels(ngfx::screen());
    print("depth: top ", pixel(px, 40, 10), ", bottom left ", pixel(px, 10, 50),
          ", bottom right ", pixel(px, 50, 50));
    print("depth: ", count(px, 0, 255, 0), " green, ", count(px, 255, 0, 0), " red, ",
          count(px, 255, 255, 255), " white");

    // Instancing: four squares from one draw, placed and colored by the
    // instance's entry in an array, its color four bytes.
    let instanced_shader = embed_slang("""
        #include <NoGraphicsAPI/shader.slang>
        struct Instance { float2 offset; uint color; };
        struct Instanced { float2* corners; Instance* instances; };
        GPU_ROOT(Instanced, root);
        struct Varyings { float4 position : SV_Position; float4 color : COLOR; };
        [shader("vertex")]
        Varyings instancedVertex(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) {
            Instance i = root.instances[instance_id];
            Varyings o;
            o.color = unpackUnorm4x8ToFloat(i.color);
            o.position = float4(i.offset + root.corners[vertex_id] * 0.25, 0.0, 1.0);
            return o;
        }
        [shader("fragment")]
        float4 instancedFragment(Varyings v) : SV_Target0 { return v.color; }
        """);
    let inst = ngfx::pipeline(instanced_shader);
    let placed: Instance[4] = [
        Instance { offset: float2 { -1.0, -1.0 }, color: [255, 0, 0, 255] },
        Instance { offset: float2 { 0.75, -1.0 }, color: [0, 255, 0, 255] },
        Instance { offset: float2 { -1.0, 0.75 }, color: [0, 0, 255, 255] },
        Instance { offset: float2 { 0.75, 0.75 }, color: [255, 255, 0, 255] },
    ];
    let per_instance = ngfx::buffer(bytes_of(placed));
    ngfx::begin_screen(float4 { 0.0, 0.0, 0.0, 1.0 });
    ngfx::bind(inst);
    ngfx::draw_indexed_instanced(ngfx::root(Instanced { corners: unit_address,
                                                        instances: ngfx::address(per_instance) }),
                                 idx, 2, 6, 4);
    ngfx::end_pass();
    px = ngfx::read_pixels(ngfx::screen());
    print("instances: ", pixel(px, 3, 3), " ", pixel(px, 60, 3), " ", pixel(px, 3, 60), " ",
          pixel(px, 60, 60), "; ", count(px, 0, 0, 0), " black");

    // No index buffer: positions from a buffer, by vertex index, covering
    // the bottom right quarter.
    let points: float4[6] = [
        float4 { 0.0, 1.0, 0.0, 1.0 }, float4 { 1.0, 1.0, 0.0, 1.0 },
        float4 { 1.0, 0.0, 0.0, 1.0 }, float4 { 0.0, 1.0, 0.0, 1.0 },
        float4 { 1.0, 0.0, 0.0, 1.0 }, float4 { 0.0, 0.0, 0.0, 1.0 },
    ];
    let stored = ngfx::buffer(bytes_of(points));
    let points_shader = embed_slang("""
        #include <NoGraphicsAPI/shader.slang>
        struct Points { float4* points; };
        GPU_ROOT(Points, root);
        [shader("vertex")]
        float4 pointsVertex(uint vertex_id : SV_VertexID) : SV_Position {
            return float4(root.points[vertex_id].xy, 0.0, 1.0);
        }
        [shader("fragment")]
        float4 pointsFragment() : SV_Target0 { return float4(1.0, 1.0, 0.0, 1.0); }
        """);
    let from_buffer = ngfx::pipeline(points_shader);
    ngfx::begin_screen(float4 { 0.0, 0.0, 0.0, 1.0 });
    ngfx::bind(from_buffer);
    ngfx::draw(ngfx::root(ngfx::address(stored)), 6);
    ngfx::end_pass();
    px = ngfx::read_pixels(ngfx::screen());
    print("buffer positions: ", count(px, 255, 255, 0), " yellow, at ", pixel(px, 48, 48));

    // Alpha blending: half-transparent red over blue.
    let blend = ngfx::pipeline(rect_shader, ngfx::PipelineDesc { blend: ngfx::BLEND_ALPHA });
    ngfx::begin_screen(float4 { 0.0, 0.0, 1.0, 1.0 });
    ngfx::bind(blend);
    rect(unit, idx, -1.0, -1.0, 1.0, 1.0, 0.0, float4 { 1.0, 0.0, 0.0, 0.5 });
    ngfx::end_pass();
    px = ngfx::read_pixels(ngfx::screen());
    let mid = (32 * W + 32) * 4;
    print("blend: half red over blue ", abs(px[mid] as i64 - 128) <= 1 && px[mid + 1] == 0 &&
                                        abs(px[mid + 2] as i64 - 128) <= 1 && px[mid + 3] == 255);
    ngfx::close();
}
