// The gfx showcase: one windowed program using as much of the gfx module as
// a small 3D scene can, and checking what it drew.
//
//   * a shadow map: a depth-only pass from the light, with depth bias, read
//     by the lit shader through a comparison sampler;
//   * the scene into a 4x multisampled HDR target, resolved: a sky from a
//     cube map, a ground with a mipmapped anisotropic checker, cubes placed
//     per instance from a storage buffer that a compute shader animates,
//     textured with an image a compute shader drew into a storage texture,
//     which was saved as a PNG and loaded back; a translucent box, a
//     wireframe one, grid lines and a trail updated every frame, and sparks
//     simulated by another compute shader, drawn additively, colored from a
//     3D noise texture read in the vertex shader;
//   * tone mapping to the screen, a picture-in-picture view of the shadow map
//     through a viewport and scissor, and icons from a texture array;
//   * an object-ID pass read back to see what landed where, input injected at
//     fixed frames, and screenshots.
//
// Time advances a fixed step per frame, so every run draws the same frames.
// This is not part of test/run_tests.py, which has to pass on machines with
// no display or GPU: run_window_test.py beside it builds and runs it on each
// backend and GPU driver, and checks its output and its screenshots.
//
//   goose gfx_showcase.goose -- [--frames N] [--out DIR] [--hidden]

import std;
import vec;
import math;
import os;
import gfx;

struct Vertex { pos: float3, normal: float3, uv: float2 }
struct LineVertex { pos: float3, color: u8[4] }

// The uniform blocks, laid out as std140 lays out the shaders' blocks.
struct Scene {
    view_proj: gfx::mat4,
    light_view_proj: gfx::mat4,
    eye: float4,
    light_dir: float4,
    cam_right: float4,
    cam_up: float4,
    params: float4,
}
struct Draw { draw: float4 }
struct Light {
    light_dir: float4,
    light_color: float4,
    ambient: float4,
    eye: float4,
    fog: float4,
    tint: float4,
}
struct View { right: float4, up: float4, forward: float4 }
struct Post { exposure: f32, vignette: f32, pad 8 }
struct Spin { time: f32, count: u32, pad 8 }
struct Sim { dt: f32, time: f32, count: u32, pad 4 }
struct Icons { place: float4 }
struct Id { id: float4 }

let CUBES = 12;
let SPARKS = 2048;
let TRAIL = 64;
let SHADOW_SIZE = 1024;
let STEP: f32 = 1.0 / 60.0;

// --- geometry and images, made on the CPU ---------------------------------------

// Vectors times a scalar: vec's operators are elementwise only.
fn times(v: float3, s: f32) -> float3 { float3 { v.x * s, v.y * s, v.z * s } }
fn times(v: float4, s: f32) -> float4 { float4 { v.x * s, v.y * s, v.z * s, v.w * s } }

// A unit cube: four vertices and two triangles per face, counterclockwise
// seen from outside, with normals and texture coordinates.
fn cube_mesh(verts: Vertex[>..]&, indices: u16[>..]&) {
    let normals: float3[6] = [float3 { 1.0, 0.0, 0.0 }, float3 { -1.0, 0.0, 0.0 },
                              float3 { 0.0, 1.0, 0.0 }, float3 { 0.0, -1.0, 0.0 },
                              float3 { 0.0, 0.0, 1.0 }, float3 { 0.0, 0.0, -1.0 }];
    let ups: float3[6] = [float3 { 0.0, 1.0, 0.0 }, float3 { 0.0, 1.0, 0.0 },
                          float3 { 0.0, 0.0, -1.0 }, float3 { 0.0, 0.0, 1.0 },
                          float3 { 0.0, 1.0, 0.0 }, float3 { 0.0, 1.0, 0.0 }];
    let corners: float2[4] = [float2 { -1.0, -1.0 }, float2 { 1.0, -1.0 }, float2 { 1.0, 1.0 },
                              float2 { -1.0, 1.0 }];
    for f in 6 {
        let n = normals[f];
        let up = ups[f];
        let right = cross(up, n);
        let base = verts.len as u16;
        for c in corners {
            let p = times(n, 0.5) + times(right, c.x * 0.5) + times(up, c.y * 0.5);
            verts.push(Vertex { pos: p, normal: n,
                                uv: float2 { (c.x + 1.0) * 0.5, (1.0 - c.y) * 0.5 } });
        }
        indices.append([base, base + 1, base + 2, base, base + 2, base + 3]);
    }
}

// A 256 x 256 two-tone checker with darker lines between the cells.
fn checker() -> u8[>..] {
    var px: u8[>..] = [];
    for y in 256 {
        for x in 256 {
            let cell = ((x / 32) + (y / 32)) % 2 == 0;
            let line = x % 32 < 2 || y % 32 < 2;
            let v: u8 = if line { 60 } else { if cell { 200 } else { 150 } };
            px.append([v, v, (v as i64 * 9 / 10) as u8, 255]);
        }
    }
    return px;
}

// The sky's color for a direction with this much upward component.
fn sky_color(y: f32) -> u8[4] {
    let horizon = float3 { 190.0, 210.0, 230.0 };
    let zenith = float3 { 40.0, 90.0, 200.0 };
    let below = float3 { 70.0, 65.0, 60.0 };
    let c = if y >= 0.0 { lerp(horizon, zenith, sqrt(y)) } else { lerp(horizon, below, sqrt(-y)) };
    return [c.x as u8, c.y as u8, c.z as u8, 255];
}

// One face of the sky cube map. Faces run +x, -x, +y, -y, +z, -z; on the
// side faces a texel row's direction points down as the row goes down.
fn sky_face(face: i64, size: i64) -> u8[>..] {
    var px: u8[>..] = [];
    for row in size {
        for col in size {
            let s = (col as f32 + 0.5) / size as f32 * 2.0 - 1.0;
            let t = (row as f32 + 0.5) / size as f32 * 2.0 - 1.0;
            let len = sqrt(1.0 + s * s + t * t);
            let y = if face == 2 { 1.0 / len } else { if face == 3 { -1.0 / len } else { -t / len } };
            px.append(sky_color(y));
        }
    }
    return px;
}

// Icons, 32 x 32 each: a disc, a ring, a triangle and a cross, on clear.
fn icon(kind: i64) -> u8[>..] {
    var px: u8[>..] = [];
    let colors: u8[4][4] = [[255, 90, 60, 255], [80, 220, 120, 255], [90, 140, 255, 255],
                            [250, 220, 70, 255]];
    let clear: u8[4] = [0, 0, 0, 0];
    for y in 32 {
        for x in 32 {
            let dx = x as f32 - 15.5;
            let dy = y as f32 - 15.5;
            let r = sqrt(dx * dx + dy * dy);
            let inside = match kind {
                0 => r < 13.0,
                1 => r < 14.0 && r > 9.0,
                2 => y > 4 && abs(dx) < (y as f32 - 4.0) * 0.55,
                _ => abs(dx) < 4.0 || abs(dy) < 4.0,
            };
            px.append(if inside { colors[kind] } else { clear });
        }
    }
    return px;
}

// --- checks -----------------------------------------------------------------------

var checks = 0;
var failures = 0;

fn expect(name: const u8[:], ok: bool, detail: const u8[:]) {
    checks++;
    if ok {
        print("check ", name, ": ok");
    } else {
        failures++;
        print("check ", name, ": FAILED ", detail);
    }
}

fn luminance(px: u8[:], i: i64) -> i64 { (px[i] as i64 * 3 + px[i + 1] as i64 * 6 + px[i + 2] as i64) / 10 }

// --- the program --------------------------------------------------------------------

fn main() {
    var frames = 150;
    var out: u8[>..] = [];
    out.append(".");
    var flags = 0;
    let argv = args();
    var ai = 1;
    while ai < argv.len {
        let a = argv[ai];
        if a == "--frames" && ai + 1 < argv.len {
            let n, ok = parse_int(argv[ai + 1]);
            if ok { frames = n; }
            ai++;
        } else if a == "--out" && ai + 1 < argv.len {
            out.clear();
            out.append(argv[ai + 1]);
            ai++;
        } else if a == "--hidden" {
            flags = gfx::WINDOW_HIDDEN;
        }
        ai++;
    }

    guard gfx::open("Goose gfx showcase", 800, 500, flags) else {
        abort(str("gfx: ", gfx::error()));
    }
    let size = gfx::screen_size();
    let w = size.x as i64;
    let h = size.y as i64;
    print("showcase: ", gfx::driver(), ", ", w, " x ", h, ", ", frames, " frames");

    // Geometry.
    var cube_verts: Vertex[>..] = [];
    var cube_indices: u16[>..] = [];
    cube_mesh(cube_verts, cube_indices);
    let cube_vb = gfx::vertex_buffer(bytes_of(cube_verts));
    let cube_ib = gfx::index_buffer(bytes_of(cube_indices));
    let quad_corners: float2[4] = [float2 { 0.0, 0.0 }, float2 { 1.0, 0.0 }, float2 { 1.0, 1.0 },
                                   float2 { 0.0, 1.0 }];
    let quad_indices: u16[6] = [0, 1, 2, 0, 2, 3];
    let quad_vb = gfx::vertex_buffer(bytes_of(quad_corners));
    let quad_ib = gfx::index_buffer(bytes_of(quad_indices));

    // Instances: three float4s each, position, scale and rotation. The
    // ground is instance 0, a flat box; the compute pass places the cubes.
    var instances: float4[>..] = [];
    instances.append([float4 { 0.0, -0.25, 0.0, 0.0 }, float4 { 24.0, 0.5, 24.0, 0.0 },
                      float4 { 0.0, 0.0, 0.0, 1.0 }]);
    for i in CUBES {
        instances.append([float4_0, float4 { 0.5, 0.5, 0.5, 0.0 }, float4 { 0.0, 0.0, 0.0, 1.0 }]);
    }
    // The translucent and the wireframe box, placed once.
    instances.append([float4 { -3.5, 1.2, -1.0, 0.0 }, float4 { 1.6, 2.4, 0.3, 0.0 },
                      float4 { 0.0, 0.38268, 0.0, 0.92388 }]);
    instances.append([float4 { 3.0, 1.0, 2.5, 0.0 }, float4 { 1.2, 1.2, 1.2, 0.0 },
                      float4 { 0.2, 0.3, 0.1, 0.927 }]);
    let glass_instance: f32 = (CUBES + 1) as f32;
    let wire_instance: f32 = (CUBES + 2) as f32;
    let inst_buf = gfx::buffer(gfx::BUFFER_STORAGE | gfx::BUFFER_COMPUTE_WRITE,
                               bytes_of(instances));

    // Sparks: two float4s each, with lives spread out so they do not all
    // respawn in the same frame.
    var sparks: float4[>..] = [];
    var r = rng { 2024 };
    for i in SPARKS {
        sparks.append([float4 { 0.0, -10.0, 0.0, r.rand_flt() as f32 * 3.0 }, float4_0]);
    }
    let spark_buf = gfx::buffer(gfx::BUFFER_STORAGE | gfx::BUFFER_COMPUTE_WRITE,
                                bytes_of(sparks));

    // Grid lines on the ground, and a trail rewritten every frame.
    var grid: LineVertex[>..] = [];
    for i in 21 {
        let v = (i - 10) as f32;
        let c: u8[4] = if i == 10 { [255, 80, 80, 255] } else { [90, 90, 90, 255] };
        grid.append([LineVertex { pos: float3 { v, 0.01, -10.0 }, color: c },
                     LineVertex { pos: float3 { v, 0.01, 10.0 }, color: c },
                     LineVertex { pos: float3 { -10.0, 0.01, v }, color: c },
                     LineVertex { pos: float3 { 10.0, 0.01, v }, color: c }]);
    }
    let grid_vb = gfx::vertex_buffer(bytes_of(grid));
    let trail_vb = gfx::buffer(gfx::BUFFER_VERTEX, TRAIL * 16, "");

    // Textures.
    // The scene's color images are sRGB, so its shaders sample linear light
    // and the tone mapping's gamma curve applies once. The icons go straight
    // to the screen, which holds gamma-encoded values already.
    let checker_tex = gfx::texture(gfx::TextureDesc { format: gfx::RGBA8_SRGB, width: 256,
                                                      height: 256, mips: 0 });
    // Each image is made into a local first: a fresh array passed straight
    // to update_texture's slice parameter trips an inlining bug.
    let checker_px = checker();
    gfx::update_texture(checker_tex, checker_px);
    gfx::generate_mips(checker_tex);
    let sky = gfx::texture_cube(64, gfx::RGBA8_SRGB, gfx::SAMPLED);
    for f in 6 {
        let face = sky_face(f, 64);
        gfx::update_texture(sky, gfx::Region { layer: f as i32 }, face);
    }
    let noise = gfx::texture3d(16, 16, 16, gfx::R8, gfx::SAMPLED);
    var noise_bytes: u8[>..] = [];
    for i in 16 * 16 * 16 { noise_bytes.push(r.rand_int(256) as u8); }
    gfx::update_texture(noise, noise_bytes);
    let icons = gfx::texture_array(32, 32, 4, gfx::RGBA8, gfx::SAMPLED);
    for k in 4 {
        let image = icon(k);
        gfx::update_texture(icons, gfx::Region { layer: k as i32 }, image);
    }
    let shadow_map = gfx::depth_target(SHADOW_SIZE, SHADOW_SIZE);
    let hdr_msaa = gfx::texture(gfx::TextureDesc { format: gfx::RGBA16F, usage: gfx::COLOR_TARGET,
                                                   width: w as i32, height: h as i32, samples: 4 });
    let depth_msaa = gfx::texture(gfx::TextureDesc { format: gfx::DEPTH, usage: gfx::DEPTH_TARGET,
                                                     width: w as i32, height: h as i32,
                                                     samples: 4 });
    let hdr = gfx::render_target(w, h, gfx::RGBA16F);
    let ids = gfx::render_target(w / 4, h / 4, gfx::RGBA8);
    let ids_depth = gfx::texture2d(w / 4, h / 4, gfx::DEPTH, gfx::DEPTH_TARGET);

    // The cubes' texture: rings a compute shader draws into a storage
    // texture, saved as a PNG and loaded back with a full mip chain.
    let rings = gfx::texture2d(128, 128, gfx::RGBA8, gfx::COMPUTE_WRITE | gfx::SAMPLED);
    let no_buffers: gfx::Buffer[>..] = [];
    let ring_tex: gfx::Texture[1] = [rings];
    gfx::begin_compute(no_buffers, ring_tex);
    gfx::bind(gfx::compute_pipeline(embed_shader("rings.comp")));
    gfx::dispatch(16, 16, 1);
    gfx::end_compute();
    let png = str(out, "/rings.png");
    let saved = gfx::save_png(rings, png);
    let loaded = gfx::load_texture(png, gfx::LOAD_MIPS | gfx::LOAD_SRGB);
    let loaded_info = gfx::texture_info(loaded);
    let drawn = gfx::read_pixels(rings);
    let reloaded = gfx::read_pixels(loaded);
    expect("png roundtrip", saved && loaded.id != 0 && drawn == reloaded && loaded_info.mips == 8,
           str("saved ", saved, ", ", loaded_info.width, " x ", loaded_info.height, " with ",
               loaded_info.mips, " mips, pixels equal ", drawn == reloaded));

    // Samplers.
    let aniso = gfx::sampler(gfx::SamplerDesc { max_anisotropy: 8.0 });
    let linear = gfx::sampler(gfx::LINEAR, gfx::CLAMP);
    let linear_repeat = gfx::sampler(gfx::LINEAR, gfx::REPEAT);
    let shadow_cmp = gfx::shadow_sampler();

    // Pipelines.
    let lit_vs = embed_shader("lit.vert");
    let lit_fs = embed_shader("lit.frag");
    let full_vs = embed_shader("fullscreen.vert");
    let line_vs = embed_shader("line.vert");
    let color_fs = embed_shader("color.frag");
    let spin = gfx::compute_pipeline(embed_shader("spin.comp"));
    let simulate = gfx::compute_pipeline(embed_shader("particles.comp"));
    let shadow = gfx::pipeline(embed_shader("shadow.vert"), embed_shader("empty.frag"),
                               gfx::PipelineDesc { cull: gfx::CULL_FRONT, depth_test: true,
                                                   depth_write: true, depth_bias: 1.0,
                                                   depth_bias_slope: 1.5 });
    let lit = gfx::pipeline(lit_vs, lit_fs, gfx::PipelineDesc { cull: gfx::CULL_BACK,
                                                                depth_test: true,
                                                                depth_write: true });
    let glass = gfx::pipeline(lit_vs, lit_fs, gfx::PipelineDesc { depth_test: true,
                                                                  blend: gfx::BLEND_ALPHA });
    let wire = gfx::pipeline(lit_vs, lit_fs, gfx::PipelineDesc { wireframe: true,
                                                                 depth_test: true,
                                                                 depth_write: true });
    let sky_pipe = gfx::pipeline(full_vs, embed_shader("sky.frag"));
    let spark_pipe = gfx::pipeline(embed_shader("particle.vert"), embed_shader("particle.frag"),
                                   gfx::PipelineDesc { depth_test: true, blend: gfx::BLEND_ADD });
    var line_desc = gfx::PipelineDesc { primitive: gfx::LINES, depth_test: true,
                                        depth_write: true };
    line_desc.formats[1] = gfx::VERTEX_UBYTE4_NORM;
    let lines = gfx::pipeline(line_vs, color_fs, line_desc);
    line_desc.primitive = gfx::LINE_STRIP;
    let strip = gfx::pipeline(line_vs, color_fs, line_desc);
    let post = gfx::pipeline(full_vs, embed_shader("post.frag"));
    let depth_view = gfx::pipeline(full_vs, embed_shader("depthview.frag"));
    let icon_pipe = gfx::pipeline(embed_shader("icon.vert"), embed_shader("icon.frag"),
                                  gfx::PipelineDesc { blend: gfx::BLEND_ALPHA });
    let id_pipe = gfx::pipeline(lit_vs, embed_shader("id.frag"),
                                gfx::PipelineDesc { cull: gfx::CULL_BACK, depth_test: true,
                                                    depth_write: true });

    let light_dir = normalize(float3 { 0.45, 1.0, 0.3 });
    let light_view = gfx::look_at(times(light_dir, 15.0), float3_0,
                                  float3 { 0.0, 1.0, 0.0 });
    let light_view_proj = gfx::mul(gfx::ortho(-9.0, 9.0, -9.0, 9.0, 1.0, 30.0), light_view);

    var t: f32 = 0.0;
    var yaw: f32 = 0.5;
    var paused = false;
    var toggles = 0;
    var yaw_by_mouse: f32 = 0.0;
    var drag_x: f32 = 0.0;
    var drags = 0;
    var releases = 0;
    var sparks_before: float4[>..] = [];
    var shots: u8[][>..] = [];
    var frame = 0;
    while gfx::frame() {
        frame++;
        // Input, injected at fixed frames: Space pauses and resumes the
        // animation, a drag with the left button turns the camera.
        if frame == 20 || frame == 32 { gfx::inject_key("Space", true); }
        if frame == 21 || frame == 33 { gfx::inject_key("Space", false); }
        if gfx::key_pressed("Space") {
            paused = !paused;
            toggles++;
        }
        if frame >= 40 && frame < 50 {
            gfx::inject_mouse(300.0 + (frame - 40) as f32 * 8.0, 200.0, gfx::MOUSE_LEFT, true);
        }
        if frame == 50 { gfx::inject_mouse(380.0, 200.0, gfx::MOUSE_LEFT, false); }
        // Turning by where the pointer has moved since the button went
        // down: the real pointer may sit anywhere over the window before the
        // injected drag begins.
        if gfx::mouse_pressed(gfx::MOUSE_LEFT) {
            drag_x = gfx::mouse_pos().x;
            drags++;
        }
        if gfx::mouse_down(gfx::MOUSE_LEFT) {
            let x = gfx::mouse_pos().x;
            let turn = (x - drag_x) * 0.004;
            drag_x = x;
            yaw += turn;
            yaw_by_mouse += turn;
        }
        if gfx::mouse_released(gfx::MOUSE_LEFT) { releases++; }
        if gfx::key_down("Left") { yaw -= 0.02; }
        if gfx::key_down("Right") { yaw += 0.02; }
        if gfx::key_pressed("Escape") { gfx::quit(); }
        if !paused { t += STEP; }

        let eye = float3 { cos(yaw) * 9.0, 4.5, sin(yaw) * 9.0 };
        let target = float3 { 0.0, 0.8, 0.0 };
        let aspect = w as f32 / h as f32;
        let fov: f32 = 1.0;
        let view = gfx::look_at(eye, target, float3 { 0.0, 1.0, 0.0 });
        let forward = normalize(target - eye);
        let right = normalize(cross(forward, float3 { 0.0, 1.0, 0.0 }));
        let up = cross(right, forward);
        let scene = Scene {
            view_proj: gfx::mul(gfx::perspective(fov, aspect, 0.1, 100.0), view),
            light_view_proj: light_view_proj,
            eye: float4 { eye.x, eye.y, eye.z, 1.0 },
            light_dir: float4 { light_dir.x, light_dir.y, light_dir.z, 0.0 },
            cam_right: float4 { right.x, right.y, right.z, 0.0 },
            cam_up: float4 { up.x, up.y, up.z, 0.0 },
            params: float4 { t, 0.0, 0.0, 0.0 },
        };

        // Compute: the cubes, then the sparks, each pass given the buffer it
        // writes.
        if frame == frames - 1 {
            sparks_before.clear();
            gfx::read_buffer(spark_buf, sparks_before, 16);
        }
        let inst_rw: gfx::Buffer[1] = [inst_buf];
        gfx::begin_compute(inst_rw);
        gfx::bind(spin);
        gfx::uniforms(gfx::COMPUTE, 0, Spin { time: t, count: (CUBES + 1) as u32 });
        gfx::dispatch(1, 1, 1);
        gfx::end_compute();
        let spark_rw: gfx::Buffer[1] = [spark_buf];
        gfx::begin_compute(spark_rw);
        gfx::bind(simulate);
        gfx::uniforms(gfx::COMPUTE, 0, Sim { dt: if paused { 0.0 } else { STEP }, time: t,
                                             count: SPARKS as u32 });
        gfx::dispatch(SPARKS / 64, 1, 1);
        gfx::end_compute();

        // The trail: a spiral rising around the fountain, rewritten whole.
        var trail: LineVertex[>..] = [];
        for i in TRAIL {
            let a = t * 2.0 + i as f32 * 0.2;
            let fade = (255 - i * 3) as u8;
            trail.push(LineVertex { pos: float3 { cos(a) * 1.2, i as f32 * 0.05, sin(a) * 1.2 },
                                    color: [fade, 200, 255, 255] });
        }
        gfx::update_buffer(trail_vb, bytes_of(trail));

        // Shadow map: depth only, from the light.
        var shadow_pass = gfx::PassDesc { depth: shadow_map };
        gfx::begin_pass(shadow_pass);
        gfx::bind(shadow);
        gfx::bind_vertex_buffer(cube_vb);
        gfx::bind_index_buffer(cube_ib, 2);
        gfx::bind_storage_buffer(gfx::VERTEX, 0, inst_buf);
        gfx::uniforms(gfx::VERTEX, 0, scene);
        gfx::uniforms(gfx::VERTEX, 1, Draw { draw: float4 { 1.0, 0.0, 0.0, 0.0 } });
        gfx::draw_indexed_instanced(36, CUBES + 2);
        gfx::end_pass();

        // The scene, multisampled, in HDR.
        var main_pass = gfx::PassDesc { depth: depth_msaa };
        main_pass.color[0] = hdr_msaa;
        main_pass.resolve[0] = hdr;
        gfx::begin_pass(main_pass);
        let view_basis = View {
            right: times(float4 { right.x, right.y, right.z, 0.0 }, tan(fov * 0.5) * aspect),
            up: times(float4 { up.x, up.y, up.z, 0.0 }, tan(fov * 0.5)),
            forward: float4 { forward.x, forward.y, forward.z, 0.0 },
        };
        gfx::bind(sky_pipe);
        gfx::bind_texture(gfx::FRAGMENT, 0, sky, linear);
        gfx::uniforms(gfx::FRAGMENT, 0, view_basis);
        gfx::draw(3);

        let light = Light {
            light_dir: float4 { light_dir.x, light_dir.y, light_dir.z, 0.0 },
            light_color: float4 { 1.6, 1.5, 1.3, 0.0 },
            ambient: float4 { 0.25, 0.3, 0.4, 0.0 },
            eye: float4 { eye.x, eye.y, eye.z, 1.0 },
            fog: float4 { 0.75, 0.82, 0.9, 0.02 },
            tint: float4_1,
        };
        gfx::bind(lit);
        gfx::bind_vertex_buffer(cube_vb);
        gfx::bind_index_buffer(cube_ib, 2);
        gfx::bind_storage_buffer(gfx::VERTEX, 0, inst_buf);
        gfx::bind_texture(gfx::FRAGMENT, 1, shadow_map, shadow_cmp);
        gfx::uniforms(gfx::VERTEX, 0, scene);
        gfx::uniforms(gfx::FRAGMENT, 0, light);
        // The ground, its checker repeated.
        gfx::bind_texture(gfx::FRAGMENT, 0, checker_tex, aniso);
        gfx::uniforms(gfx::VERTEX, 1, Draw { draw: float4 { 0.0, 10.0, 0.0, 0.0 } });
        gfx::draw_indexed(36);
        // The cubes, in the loaded image.
        gfx::bind_texture(gfx::FRAGMENT, 0, loaded, linear_repeat);
        gfx::uniforms(gfx::VERTEX, 1, Draw { draw: float4 { 1.0, 0.0, 0.0, 0.0 } });
        gfx::draw_indexed_instanced(36, CUBES);
        // A wireframe box.
        gfx::bind(wire);
        gfx::uniforms(gfx::VERTEX, 1, Draw { draw: float4 { wire_instance, 0.0, 0.0, 0.0 } });
        var wire_light = light;
        wire_light.tint = float4 { 0.4, 1.0, 0.5, 1.0 };
        gfx::uniforms(gfx::FRAGMENT, 0, wire_light);
        gfx::draw_indexed(36);

        // Lines: the grid, and the trail.
        gfx::bind(lines);
        gfx::bind_vertex_buffer(grid_vb);
        gfx::draw(grid.len);
        gfx::bind(strip);
        gfx::bind_vertex_buffer(trail_vb);
        gfx::draw(TRAIL);

        // Translucent things last: the glass box, then the sparks.
        gfx::bind(glass);
        gfx::bind_vertex_buffer(cube_vb);
        gfx::bind_index_buffer(cube_ib, 2);
        gfx::bind_texture(gfx::FRAGMENT, 0, checker_tex, aniso);
        gfx::uniforms(gfx::VERTEX, 1, Draw { draw: float4 { glass_instance, 0.0, 0.0, 0.0 } });
        var glass_light = light;
        glass_light.tint = float4 { 0.5, 0.8, 1.0, 0.35 };
        gfx::uniforms(gfx::FRAGMENT, 0, glass_light);
        gfx::draw_indexed(36);
        gfx::bind(spark_pipe);
        gfx::bind_texture(gfx::VERTEX, 0, noise, linear_repeat);
        gfx::bind_storage_buffer(gfx::VERTEX, 0, spark_buf);
        gfx::draw_instanced(6, SPARKS);
        gfx::end_pass();

        // To the screen: tone mapped, then the shadow map in a corner, then
        // the icons.
        gfx::begin_pass(gfx::screen(), gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 1.0 });
        gfx::bind(post);
        gfx::bind_texture(gfx::FRAGMENT, 0, hdr, linear);
        gfx::uniforms(gfx::FRAGMENT, 0, Post { exposure: 1.2, vignette: 0.8 });
        gfx::draw(3);
        let pip = h / 4;
        gfx::viewport((w - pip - 10) as f32, 10.0, pip as f32, pip as f32);
        gfx::scissor(w - pip - 10, 10, pip, pip);
        gfx::bind(depth_view);
        gfx::bind_texture(gfx::FRAGMENT, 0, shadow_map, linear);
        gfx::draw(3);
        gfx::viewport(0.0, 0.0, w as f32, h as f32);
        gfx::scissor(0, 0, 0, 0);
        gfx::scissor(0, 0, w, h);
        gfx::bind(icon_pipe);
        gfx::bind_vertex_buffer(quad_vb);
        gfx::bind_index_buffer(quad_ib, 2);
        gfx::bind_texture(gfx::FRAGMENT, 0, icons, linear);
        gfx::uniforms(gfx::VERTEX, 0, Icons { place: float4 { -0.96, -0.96, 0.12, 0.15 } });
        gfx::draw_indexed_instanced(6, 4);
        gfx::end_pass();

        gfx::set_title(str("Goose gfx showcase: frame ", frame));
        if frame == 10 || frame == frames / 2 || frame == frames {
            let shot .= shots.push(str(out, "/shot_", frame, ".png"));
            gfx::screenshot(shot);
        }
        if frame < frames { continue; }

        // What landed where: the meshes drawn again with a flat color each,
        // small, and read back.
        var id_pass = gfx::PassDesc { depth: ids_depth, color_value: float4 { 0.0, 0.0, 0.0, 0.0 } };
        id_pass.color[0] = ids;
        gfx::begin_pass(id_pass);
        gfx::bind(id_pipe);
        gfx::bind_vertex_buffer(cube_vb);
        gfx::bind_index_buffer(cube_ib, 2);
        gfx::bind_storage_buffer(gfx::VERTEX, 0, inst_buf);
        gfx::uniforms(gfx::VERTEX, 0, scene);
        gfx::uniforms(gfx::VERTEX, 1, Draw { draw: float4 { 0.0, 0.0, 0.0, 0.0 } });
        gfx::uniforms(gfx::FRAGMENT, 0, Id { id: float4 { 1.0, 0.0, 0.0, 1.0 } });
        gfx::draw_indexed(36);
        gfx::uniforms(gfx::VERTEX, 1, Draw { draw: float4 { 1.0, 0.0, 0.0, 0.0 } });
        gfx::uniforms(gfx::FRAGMENT, 0, Id { id: float4 { 0.0, 1.0, 0.0, 1.0 } });
        gfx::draw_indexed_instanced(36, CUBES);
        gfx::end_pass();
        let id_px = gfx::read_pixels(ids);
        var ground_px = 0;
        var cube_px = 0;
        for i in id_px.len / 4 {
            if id_px[i * 4] == 255 { ground_px++; }
            if id_px[i * 4 + 1] == 255 { cube_px++; }
        }
        let id_total = id_px.len / 4;
        expect("ground in view", ground_px > id_total / 5,
               str(ground_px, " of ", id_total, " pixels"));
        expect("cubes in view", cube_px > id_total / 100, str(cube_px, " of ", id_total, " pixels"));

        // The screen itself.
        let px = gfx::read_pixels(gfx::screen());
        expect("screen size", px.len == w * h * 4, str(px.len, " bytes"));
        var sky_r = 0;
        var sky_b = 0;
        for x in w / 4..w * 3 / 4 {
            let i = (8 * w + x) * 4;
            sky_r += px[i] as i64;
            sky_b += px[i + 2] as i64;
        }
        expect("sky along the top", sky_b > sky_r, str("red ", sky_r, ", blue ", sky_b));
        var lo = 255;
        var hi = 0;
        for x in w / 4..w * 3 / 4 {
            let l = luminance(px, ((h - 40) * w + x) * 4);
            lo = min(lo, l);
            hi = max(hi, l);
        }
        expect("checker on the ground", hi - lo > 30, str("luminance ", lo, " to ", hi));
        var grays = 0;
        var gray_lo = 255;
        var gray_hi = 0;
        for y in 12..12 + pip - 4 {
            for x in w - pip - 8..w - 12 {
                let i = (y * w + x) * 4;
                if px[i] == px[i + 1] && px[i + 1] == px[i + 2] {
                    grays++;
                    gray_lo = min(gray_lo, px[i] as i64);
                    gray_hi = max(gray_hi, px[i] as i64);
                }
            }
        }
        expect("shadow map in the corner", grays > (pip - 4) * (pip - 20) * 9 / 10 &&
                                           gray_hi - gray_lo > 20,
               str(grays, " gray pixels from ", gray_lo, " to ", gray_hi));
        var icon_hits = 0;
        for x in 0..w / 4 {
            let i = ((h - h * 2 / 100 - 10) * w + x) * 4;
            if px[i] > 200 && px[i + 1] < 150 { icon_hits++; }
        }
        expect("icons along the bottom", icon_hits > 3, str(icon_hits, " red icon pixels"));
        var colors: u32[>..] = [];
        var step = 0;
        for i in w * h {
            step++;
            if step % 7 != 0 { continue; }
            colors.push((px[i * 4] as u32 << 16) | (px[i * 4 + 1] as u32 << 8) |
                        px[i * 4 + 2] as u32);
        }
        sort(colors);
        var distinct = 0;
        for c, i in colors {
            if i == 0 || c != colors[i - 1] { distinct++; }
        }
        expect("detail", distinct > 500, str(distinct, " distinct colors sampled"));

        var sparks_after: float4[>..] = [];
        gfx::read_buffer(spark_buf, sparks_after, 16);
        var moved = 0;
        for i in 16 {
            if sparks_after[i] != sparks_before[i] { moved++; }
        }
        expect("sparks move", moved > 8, str(moved, " of 16 values changed in a frame"));
        // Nine moves of 8 pixels while the button is down.
        expect("input", toggles == 2 && !paused && drags == 1 && releases == 1 &&
                        abs(yaw_by_mouse - 0.288) < 0.01,
               str(toggles, " toggles, ", drags, " drags, ", releases, " releases, turned ",
                   yaw_by_mouse, " by the mouse"));
        var written = 0;
        for s in shots {
            if file_exists(s) { written++; }
        }
        expect("screenshots", written == 3, str(written, " of ", shots.len, " written"));
        gfx::quit();
    }

    gfx::release(loaded);
    gfx::release(rings);
    gfx::release(lit);
    gfx::release(spin);
    gfx::release(aniso);
    gfx::release(trail_vb);
    gfx::close();
    if failures > 0 {
        print("showcase: ", failures, " of ", checks, " checks FAILED");
        exit(1);
    }
    print("showcase: all ", checks, " checks passed");
}
