// Thousands of boxes raining into a walled arena: the physics module
// (Box3D, docs/stdlib.md) moves them, the gfx module draws them.
//
// Boxes of every size and color fall from high above the whole arena. They
// tumble onto two pyramids and each other, and a long arm turning in the
// middle sweeps the heap around. Once the arena holds as many boxes as asked
// for, the oldest are lifted back up, so the rain never stops. Box3D steps
// the world on as many worker threads as the machine has, up to eight.
//
// Each frame the bodies' transforms come back in one call and go straight
// into an instance buffer: one unit cube drawn once per box, moved, turned
// and colored in the vertex shader, and lit by a sun whose shadow map is
// drawn from the same buffer first. The shaders are GLSL, written below as
// """ strings that embed_shader compiles along with the program.
//
// Space sets off an explosion under the heap; drag with the left mouse
// button to look around and use the wheel to zoom; Up and Down pour faster
// and slower; Escape quits.
//
//   goose 28_physics_boxes.goose
//   goose 28_physics_boxes.goose -- --boxes 20000
//
// `--frames N` stops after N frames and saves the last as physics_boxes.png;
// `--rate N` drops N boxes a frame. Built from the generated C, the program
// links both native layers, whose paths goose prints as response files:
//   goose -o physics_boxes.c 28_physics_boxes.goose
//   cl physics_boxes.c @<goose --gfx-link msvc> @<goose --physics-link msvc>
//   cc physics_boxes.c -o physics_boxes @<goose --gfx-link cc> @<goose --physics-link cc>

import std;
import vec;
import math;
import os;
import gfx;
import physics;

// A corner of the unit cube, with its face's normal.
struct Vertex { pos: float3, normal: float3 }
// One box as the vertex shader reads it, per instance: where, how big, how
// turned, what color.
struct Instance { position: float3, half: float3, rotation: physics::Quat, color: u8[4] }
// The uniform blocks of the shaders: two column-major matrices, and the
// light, each already laid out as std140 wants.
struct Scene { view_proj: gfx::mat4, light_view_proj: gfx::mat4 }
struct Light { sun: float4, eye: float4, haze: float4 }
// What a falling box looks like, by its body.
struct Look { half: float3, color: u8[4] }

let ARENA: f32 = 18.0;          // half the floor's width, inside the walls
let SHADOW_SIZE = 2048;
// The rain falls from a grid of cells over the arena, which it visits in a
// scattered order that comes back to a cell only after all the others:
// by then that cell's last box has fallen well clear, so no box starts
// inside another.
let CELLS = 24;
let CELL: f32 = 1.3;
let DT: f32 = 1.0 / 60.0;

fn f3(x: f32) -> float3 { float3 { x, x, x } }

// The unit cube: four corners and two triangles a face, counterclockwise
// seen from outside.
fn cube(verts: Vertex[>..]&, indices: u16[>..]&) {
    let axes: float3[3] = [float3 { 1.0, 0.0, 0.0 }, float3 { 0.0, 1.0, 0.0 },
                           float3 { 0.0, 0.0, 1.0 }];
    for f in 6 {
        let sign: f32 = if f % 2 == 0 { 1.0 } else { -1.0 };
        let n = axes[f / 2] * f3(sign);
        let u = axes[(f / 2 + 1) % 3] * f3(sign);
        let v = cross(n, u);
        let base = verts.len as u16;
        let corners: float2[4] = [float2 { -1.0, -1.0 }, float2 { 1.0, -1.0 },
                                  float2 { 1.0, 1.0 }, float2 { -1.0, 1.0 }];
        for c in corners { verts.push(Vertex { pos: n + u * f3(c.x) + v * f3(c.y), normal: n }); }
        indices.append([base, base + 1, base + 2, base, base + 2, base + 3]);
    }
}

// A bright color from around the color wheel, h from 0 to 1.
fn hue(h: f32) -> u8[4] {
    let r = clamp(abs(h * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    let g = clamp(2.0 - abs(h * 6.0 - 2.0), 0.0, 1.0);
    let b = clamp(2.0 - abs(h * 6.0 - 4.0), 0.0, 1.0);
    [round(50.0 + 190.0 * r) as u8, round(50.0 + 190.0 * g) as u8, round(50.0 + 190.0 * b) as u8,
     255]
}

// A random number from lo to hi; the f64 rounds to the nearest f32.
fn flt(r: rng&, lo: f32, hi: f32) -> f32 { lo + (hi - lo) * r.rand_flt() as! f32 }

// Where the n-th box starts: 263 shares no factor with the 576 cells, so
// stepping by it visits them all before any twice.
fn raindrop(n: i64, r: rng&) -> float3 {
    let c = n * 263 % (CELLS * CELLS);
    let middle = (CELLS - 1) as f32 * 0.5;
    float3 { ((c % CELLS) as f32 - middle) * CELL + flt(r, -0.15, 0.15), flt(r, 24.0, 25.0),
             ((c / CELLS) as f32 - middle) * CELL + flt(r, -0.15, 0.15) }
}

// --- shaders -----------------------------------------------------------------

// The first part of both vertex shaders: a corner of the cube from vertex
// buffer 0, an Instance from buffer 1, the Scene, and where that corner of
// that box is in the world. Both shaders read the same buffers, so both
// declare every input, which is what the pipeline lays the buffers out by.
let box_input = """
    layout(location = 0) in vec3 a_pos;
    layout(location = 1) in vec3 a_normal;
    layout(location = 2) in vec3 i_position;
    layout(location = 3) in vec3 i_half;
    layout(location = 4) in vec4 i_rotation;
    layout(location = 5) in vec4 i_color;
    layout(set = 1, binding = 0) uniform Scene {
        mat4 view_proj;
        mat4 light_view_proj;
    };

    // Rotates v by the unit quaternion q.
    vec3 rotate_q(vec4 q, vec3 v) {
        vec3 t = 2.0 * cross(q.xyz, v);
        return v + q.w * t + cross(q.xyz, t);
    }

    vec3 world_corner() {
        return i_position + rotate_q(i_rotation, a_pos * i_half);
    }
    """;

// A box seen from the camera, with what its light needs.
let boxes_vert = """
    layout(location = 0) out vec3 v_world;
    layout(location = 1) out vec3 v_normal;
    layout(location = 2) out vec4 v_color;
    layout(location = 3) out vec4 v_shadow;
    layout(location = 4) out vec3 v_local;
    void main() {
        vec3 world = world_corner();
        v_world = world;
        v_normal = rotate_q(i_rotation, a_normal);
        v_color = i_color;
        v_shadow = light_view_proj * vec4(world, 1.0);
        // Where on the face this corner is, from -1 to 1, for darkening edges.
        v_local = a_pos * 2.0;
        gl_Position = view_proj * vec4(world, 1.0);
    }
    """;

// The boxes' light: the sun, softened by a shadow map read through hardware
// depth comparison, sky light from above, darker edges so that boxes in a
// heap stay apart, and haze with distance.
let boxes_frag = """
    layout(location = 0) in vec3 v_world;
    layout(location = 1) in vec3 v_normal;
    layout(location = 2) in vec4 v_color;
    layout(location = 3) in vec4 v_shadow;
    layout(location = 4) in vec3 v_local;
    layout(location = 0) out vec4 result;
    layout(set = 2, binding = 0) uniform sampler2DShadow shadow_map;
    layout(set = 3, binding = 0) uniform Light {
        vec4 sun;       // xyz: toward the sun
        vec4 eye;
        vec4 haze;      // rgb: its color, w: density
    };

    float sunlit(vec4 s) {
        vec3 p = s.xyz / s.w;
        vec2 uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
        float texel = 1.0 / 2048.0;
        float lit = 0.0;
        lit += texture(shadow_map, vec3(uv + vec2(-texel, -texel), p.z));
        lit += texture(shadow_map, vec3(uv + vec2(texel, -texel), p.z));
        lit += texture(shadow_map, vec3(uv + vec2(-texel, texel), p.z));
        lit += texture(shadow_map, vec3(uv + vec2(texel, texel), p.z));
        return lit * 0.25;
    }

    void main() {
        vec3 n = normalize(v_normal);
        float diffuse = max(dot(n, sun.xyz), 0.0) * sunlit(v_shadow);
        float sky = 0.35 + 0.25 * n.y;
        // The two coordinates across this face nearest 1 in size make an edge.
        vec3 a = abs(v_local);
        float across = max(max(min(a.x, a.y), min(a.y, a.z)), min(a.x, a.z));
        float edge = 1.0 - 0.35 * smoothstep(0.85, 1.0, across);
        vec3 light = vec3(1.0, 0.95, 0.85) * diffuse + vec3(0.55, 0.65, 0.8) * sky;
        vec3 color = v_color.rgb * light * edge;
        float dist = length(eye.xyz - v_world);
        result = vec4(mix(color, haze.rgb, 1.0 - exp(-dist * haze.w)), 1.0);
    }
    """;

// A box seen from the sun, for the shadow map: only its depth.
let shadow_vert = """
    void main() {
        gl_Position = light_view_proj * vec4(world_corner(), 1.0);
    }
    """;

fn main() {
    var frames = -1;
    var max_boxes = 10000;
    var rate = 25;
    let argv = args();
    var a = 1;
    while a + 1 < argv.len {
        let n, ok = parse_int(argv[a + 1]);
        if ok && argv[a] == "--frames" { frames = n; }
        if ok && argv[a] == "--boxes" { max_boxes = max(n, 1); }
        if ok && argv[a] == "--rate" { rate = max(n, 0); }
        a += 2;
    }
    guard gfx::open("Goose: boxes", 1280, 720, gfx::WINDOW_RESIZABLE) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }

    // --- the world -----------------------------------------------------------
    let world = physics::create_world(physics::WorldDef {
        workers: min(hardware_threads(), 8) as i32,
        capacity: physics::Capacity { dynamic_bodies: max_boxes as i32 } });
    // The floor and four walls, all shapes of one static body; the arena's
    // pieces are drawn as boxes too.
    var props: Instance[>..] = [];
    let arena = physics::create_body(world, physics::BodyDef {});
    let wall_color: u8[4] = [150, 155, 165, 255];
    let pieces: float3[10] = [
        float3 { 0.0, -0.5, 0.0 }, float3 { ARENA + 1.0, 0.5, ARENA + 1.0 },
        float3 { 0.0, 1.5, ARENA + 0.5 }, float3 { ARENA + 1.0, 1.5, 0.5 },
        float3 { 0.0, 1.5, -ARENA - 0.5 }, float3 { ARENA + 1.0, 1.5, 0.5 },
        float3 { ARENA + 0.5, 1.5, 0.0 }, float3 { 0.5, 1.5, ARENA + 1.0 },
        float3 { -ARENA - 0.5, 1.5, 0.0 }, float3 { 0.5, 1.5, ARENA + 1.0 }];
    for i in 5 {
        let at = pieces[i * 2];
        let half = pieces[i * 2 + 1];
        physics::create_box_shape(arena, physics::ShapeDef {}, half,
                                  physics::Transform { p: at, q: physics::quat_identity });
        props.push(Instance { position: at, half: half, rotation: physics::quat_identity,
                              color: if i == 0 { [96, 104, 112, 255] } else { wall_color } });
    }

    // The arm: kinematic, so it turns at its own speed whatever it hits.
    let arm_half = float3 { ARENA * 0.55, 0.6, 0.35 };
    let arm = physics::create_body(world, physics::BodyDef {
        body_type: physics::KINEMATIC, position: float3 { 0.0, 0.6, 0.0 },
        angular_velocity: float3 { 0.0, 0.5, 0.0 } });
    physics::create_box_shape(arm, physics::ShapeDef {}, arm_half);

    // Two pyramids for the pour to bury and the arm to knock down.
    var bodies: physics::Body[>..] = [];
    var looks: Look[>..] = [];
    for p in 2 {
        let center = if p == 0 { float3 { 8.0, 0.0, 5.0 } } else { float3 { -7.0, 0.0, -8.0 } };
        let rows = 8;
        for row in rows {
            for k in rows - row {
                let at = center + float3 { (k as f32 - (rows - row - 1) as f32 * 0.5) * 1.02,
                                           0.5 + row as f32 * 1.0, 0.0 };
                let b = physics::create_body(world, physics::BodyDef { body_type: physics::DYNAMIC,
                                                                       position: at });
                physics::create_box_shape(b, physics::ShapeDef {}, f3(0.5));
                bodies.push(b);
                let shade = (180 + row * 9) as u8;
                looks.push(Look { half: f3(0.5), color: if p == 0 { [shade, shade, 90, 255] }
                                                        else { [90, shade, shade, 255] } });
            }
        }
    }
    let pyramid_boxes = bodies.len;

    // --- drawing -------------------------------------------------------------
    var verts: Vertex[>..] = [];
    var indices: u16[>..] = [];
    cube(verts, indices);
    let cube_vb = gfx::vertex_buffer(bytes_of(verts));
    let cube_ib = gfx::index_buffer(bytes_of(indices));
    // Room for every instance there will ever be: filled from the start
    // each frame.
    let one: Instance[1] = [default<Instance>()];
    let none: u8[>..] = [];
    let instance_bytes = bytes_of(one).len;
    let inst_vb = gfx::buffer(gfx::BUFFER_VERTEX,
                              (props.len + 1 + pyramid_boxes + max_boxes) * instance_bytes, none);
    // Corners from buffer 0, the rest per instance from buffer 1; the color
    // comes as four bytes.
    var desc = gfx::PipelineDesc { cull: gfx::CULL_BACK, depth_test: true, depth_write: true,
                                   instance_location: 2 };
    desc.formats[5] = gfx::VERTEX_UBYTE4_NORM;
    let boxes_pipe = gfx::pipeline(embed_shader("vert", box_input, boxes_vert),
                                   embed_shader("frag", boxes_frag), desc);
    var shadow_desc = gfx::PipelineDesc { cull: gfx::CULL_FRONT, depth_test: true,
                                          depth_write: true, instance_location: 2,
                                          depth_bias: 1.0, depth_bias_slope: 1.5 };
    shadow_desc.formats[5] = gfx::VERTEX_UBYTE4_NORM;
    // A pass with only depth still needs a fragment shader: an empty one.
    let shadow_pipe = gfx::pipeline(embed_shader("vert", box_input, shadow_vert),
                                    embed_shader("frag", "void main() { }"), shadow_desc);
    let shadow_map = gfx::depth_target(SHADOW_SIZE, SHADOW_SIZE);
    let shadow_cmp = gfx::shadow_sampler();

    // The sun, and a view of the whole arena from it.
    let sun = normalize(float3 { 0.45, 1.0, 0.3 });
    let up = float3 { 0.0, 1.0, 0.0 };
    let light_view_proj = gfx::mul(gfx::ortho(-30.0, 30.0, -30.0, 30.0, 1.0, 100.0),
                                   gfx::look_at(sun * f3(50.0), float3_0, up));

    // --- the pour ------------------------------------------------------------
    var r = rng { 2026 };
    var instances: Instance[>..] = [];
    var transforms: physics::Transform[>..] = [];
    var dropped = 0;
    var oldest = 0;
    var yaw: f32 = 0.6;
    var pitch: f32 = 0.45;
    var distance: f32 = 48.0;
    var drawn = 0;
    var stats_time: f64 = 0.0;
    var stats_frames = 0;
    while gfx::frame() {
        if gfx::key_pressed("Escape") { break; }
        if gfx::key_pressed("Up") { rate = min(rate * 2, 400); }
        if gfx::key_pressed("Down") { rate = rate / 2; }
        if gfx::key_pressed("Space") {
            physics::explode(world, physics::ExplosionDef { position: float3 { 0.0, -1.0, 0.0 },
                                                            radius: 14.0, falloff: 6.0,
                                                            impulse_per_area: 60.0 });
        }
        if gfx::mouse_down(gfx::MOUSE_LEFT) {
            let d = gfx::mouse_delta();
            yaw += d.x * 0.005;
            pitch = clamp(pitch + d.y * 0.005, 0.05, 1.45);
        } else {
            yaw += 0.001;
        }
        distance = clamp(distance - gfx::mouse_wheel() * 2.0, 10.0, 120.0);

        // New boxes, and once there are enough, the oldest lifted back up.
        for k in rate {
            let at = raindrop(dropped, r);
            let turn = physics::quat_axis_angle(float3 { flt(r, -1.0, 1.0), flt(r, -1.0, 1.0),
                                                         flt(r, -1.0, 1.0) } + f3(0.01),
                                                flt(r, 0.0, 3.14));
            let velocity = float3 { flt(r, -0.3, 0.3), flt(r, -5.0, -3.0), flt(r, -0.3, 0.3) };
            let spin = float3 { flt(r, -3.0, 3.0), flt(r, -3.0, 3.0), flt(r, -3.0, 3.0) };
            let h = dropped as f32 * 0.0007 + flt(r, 0.0, 0.05);
            let look = Look { half: float3 { flt(r, 0.18, 0.45), flt(r, 0.18, 0.45),
                                             flt(r, 0.18, 0.45) },
                              color: hue(h - floor(h)) };
            if bodies.len - pyramid_boxes < max_boxes {
                let b = physics::create_body(world, physics::BodyDef {
                    body_type: physics::DYNAMIC, position: at, rotation: turn,
                    linear_velocity: velocity, angular_velocity: spin });
                physics::create_box_shape(b, physics::ShapeDef {}, look.half);
                bodies.push(b);
                looks.push(look);
            } else {
                // The oldest box, back up; it keeps its shape.
                let i = pyramid_boxes + oldest;
                oldest = (oldest + 1) % max_boxes;
                physics::set_transform(bodies[i], at, turn);
                physics::set_linear_velocity(bodies[i], velocity);
                physics::set_angular_velocity(bodies[i], spin);
                physics::set_awake(bodies[i], true);
                looks[i].color = look.color;
            }
            dropped++;
        }

        let before = clock();
        physics::step(world, DT, 4);
        stats_time += clock() - before;
        stats_frames++;

        // Every body's transform in one call, into the instances.
        transforms.resize(bodies.len, physics::transform_identity);
        physics::transforms(bodies, transforms);
        // A box knocked over a wall has nothing under it: it rains again.
        for i in bodies.len {
            if transforms[i].p.y < -10.0 {
                transforms[i].p = raindrop(dropped, r);
                dropped++;
                physics::set_transform(bodies[i], transforms[i].p, transforms[i].q);
                physics::set_linear_velocity(bodies[i], float3 { 0.0, -4.0, 0.0 });
            }
        }
        instances.clear();
        instances.append(props);
        let arm_at = physics::transform(arm);
        instances.push(Instance { position: arm_at.p, half: arm_half, rotation: arm_at.q,
                                  color: [230, 90, 60, 255] });
        for i in bodies.len {
            instances.push(Instance { position: transforms[i].p, half: looks[i].half,
                                      rotation: transforms[i].q, color: looks[i].color });
        }
        gfx::update_buffer(inst_vb, bytes_of(instances));

        let size = gfx::screen_size();
        let aspect = size.x as f32 / size.y as f32;
        let eye = float3 { distance * cos(pitch) * cos(yaw), distance * sin(pitch),
                           distance * cos(pitch) * sin(yaw) };
        let scene = Scene {
            view_proj: gfx::mul(gfx::perspective(0.9, aspect, 0.5, 300.0),
                                gfx::look_at(eye, float3 { 0.0, 2.0, 0.0 }, up)),
            light_view_proj: light_view_proj };

        // The shadow map, then the boxes lit and shadowed.
        gfx::begin_pass(gfx::PassDesc { depth: shadow_map });
        gfx::bind(shadow_pipe);
        gfx::bind_vertex_buffer(0, cube_vb, 0);
        gfx::bind_vertex_buffer(1, inst_vb, 0);
        gfx::bind_index_buffer(cube_ib, 2);
        gfx::uniforms(gfx::VERTEX, 0, scene);
        gfx::draw_indexed_instanced(indices.len, instances.len);
        gfx::end_pass();

        gfx::begin_screen(float4 { 0.62, 0.72, 0.84, 1.0 });
        gfx::bind(boxes_pipe);
        gfx::bind_vertex_buffer(0, cube_vb, 0);
        gfx::bind_vertex_buffer(1, inst_vb, 0);
        gfx::bind_index_buffer(cube_ib, 2);
        gfx::bind_texture(gfx::FRAGMENT, 0, shadow_map, shadow_cmp);
        gfx::uniforms(gfx::VERTEX, 0, scene);
        gfx::uniforms(gfx::FRAGMENT, 0, Light { sun: float4 { sun.x, sun.y, sun.z, 0.0 },
                                                eye: float4 { eye.x, eye.y, eye.z, 1.0 },
                                                haze: float4 { 0.62, 0.72, 0.84, 0.006 } });
        gfx::draw_indexed_instanced(indices.len, instances.len);
        gfx::end_pass();

        if stats_frames == 30 {
            gfx::set_title(str("Goose: ", bodies.len - pyramid_boxes, " boxes, ",
                               physics::awake_body_count(world), " awake, a step takes ",
                               round(stats_time / 30.0 * 1000.0) as i64, " ms on ",
                               physics::worker_count(world), " threads (",
                               rate, " a frame; Space, mouse, Up/Down)"));
            stats_time = 0.0;
            stats_frames = 0;
        }
        drawn++;
        if drawn == frames { break; }
    }

    // How the last frame left things: every box still in the arena, the
    // pyramids knocked about, the arm turned.
    physics::transforms(bodies, transforms);
    var inside = true;
    var toppled = 0;
    for i in bodies.len {
        let p = transforms[i].p;
        inside = inside && abs(p.x) < ARENA + 1.0 && abs(p.z) < ARENA + 1.0 && p.y > -0.1;
    }
    for i in pyramid_boxes {
        if length(physics::linear_velocity(bodies[i])) > 0.01 { toppled++; }
    }
    let arm_turn = physics::rotation(arm);
    let saved = gfx::screenshot("physics_boxes.png");
    print("drew ", drawn, " frames; dropped ", dropped, " boxes; all in the arena: ", inside,
          "; the arm turned ", round(2.0 * acos(arm_turn.w) * 57.29578) as i64,
          " degrees; saved physics_boxes.png: ", saved);
    write_stderr(str(toppled, " of ", pyramid_boxes, " pyramid boxes moving\n"));
    physics::destroy(world);
    gfx::close();
}
