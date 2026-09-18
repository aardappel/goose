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

// --- shaders, GLSL that embed_shader compiles with the program ----------------------

// The per-frame uniform block every 3D vertex shader reads (vertex set 1,
// binding 0), and the instance transform they share: those shaders' first
// part.
let scene_glsl = """
    layout(set = 1, binding = 0) uniform Scene {
        mat4 view_proj;
        mat4 light_view_proj;
        vec4 eye;
        vec4 light_dir;         // toward the light
        vec4 cam_right;
        vec4 cam_up;
        vec4 params;            // x: time
    };

    // Rotates v by the unit quaternion q.
    vec3 rotate_q(vec4 q, vec3 v) {
        vec3 t = 2.0 * cross(q.xyz, v);
        return v + q.w * t + cross(q.xyz, t);
    }
    """;

// A procedural image into a storage texture: colored rings in a dark frame.
// The showcase saves it as a PNG and loads it back as the cubes' texture.
let rings_comp = """
    layout(local_size_x = 8, local_size_y = 8) in;
    layout(set = 1, binding = 0, rgba8) uniform writeonly image2D image;
    void main() {
        ivec2 p = ivec2(gl_GlobalInvocationID.xy);
        vec2 uv = vec2(p) / 128.0 - 0.5;
        float r = length(uv) * 12.0;
        vec3 c = 0.5 + 0.5 * cos(vec3(0.0, 2.1, 4.2) + r);
        float edge = step(0.45, max(abs(uv.x), abs(uv.y)));
        imageStore(image, p, vec4(mix(c, vec3(0.1), edge), 1.0));
    }
    """;

// A mesh placed by one instance of a storage buffer of three vec4s each:
// position, scale, rotation. The draw's first instance comes from a uniform,
// since SV_InstanceID on Direct3D 12 does not count the draw's own.
let lit_vert = """
    layout(location = 0) in vec3 a_pos;
    layout(location = 1) in vec3 a_normal;
    layout(location = 2) in vec2 a_uv;
    layout(std430, set = 0, binding = 0) readonly buffer Instances { vec4 inst[]; };
    layout(set = 1, binding = 1) uniform Draw { vec4 draw; };   // x: first instance, y: uv tiling
    layout(location = 0) out vec3 v_world;
    layout(location = 1) out vec3 v_normal;
    layout(location = 2) out vec2 v_uv;
    layout(location = 3) out vec4 v_shadow;
    void main() {
        int i = (int(draw.x) + gl_InstanceIndex) * 3;
        vec3 pos = inst[i].xyz;
        vec3 scale = inst[i + 1].xyz;
        vec4 rot = inst[i + 2];
        vec3 world = pos + rotate_q(rot, a_pos * scale);
        v_world = world;
        v_normal = normalize(rotate_q(rot, a_normal / scale));
        v_uv = draw.y > 0.0 ? a_uv * draw.y : a_uv;
        v_shadow = light_view_proj * vec4(world, 1.0);
        gl_Position = view_proj * vec4(world, 1.0);
    }
    """;

// Diffuse and specular light from one direction, shadowed by a shadow map
// through hardware depth comparison (four taps), with distance fog.
let lit_frag = """
    layout(location = 0) in vec3 v_world;
    layout(location = 1) in vec3 v_normal;
    layout(location = 2) in vec2 v_uv;
    layout(location = 3) in vec4 v_shadow;
    layout(location = 0) out vec4 result;
    layout(set = 2, binding = 0) uniform sampler2D albedo;
    layout(set = 2, binding = 1) uniform sampler2DShadow shadow_map;
    layout(set = 3, binding = 0) uniform Light {
        vec4 light_dir;
        vec4 light_color;
        vec4 ambient;
        vec4 eye;
        vec4 fog;                // rgb: color, w: density
        vec4 tint;               // multiplies the result; a: its alpha
    };
    float shadowed(vec4 s) {
        vec3 p = s.xyz / s.w;
        vec2 uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
        float texel = 1.0 / 1024.0;
        float lit = 0.0;
        lit += texture(shadow_map, vec3(uv + vec2(-texel, -texel), p.z));
        lit += texture(shadow_map, vec3(uv + vec2(texel, -texel), p.z));
        lit += texture(shadow_map, vec3(uv + vec2(-texel, texel), p.z));
        lit += texture(shadow_map, vec3(uv + vec2(texel, texel), p.z));
        return lit * 0.25;
    }
    void main() {
        vec3 n = normalize(v_normal);
        vec3 l = normalize(light_dir.xyz);
        vec3 base = texture(albedo, v_uv).rgb * tint.rgb;
        float diffuse = max(dot(n, l), 0.0) * shadowed(v_shadow);
        vec3 view = normalize(eye.xyz - v_world);
        float spec = pow(max(dot(reflect(-l, n), view), 0.0), 32.0) * diffuse;
        vec3 color = base * (ambient.rgb + light_color.rgb * diffuse) +
                     light_color.rgb * spec * 0.4;
        float dist = length(eye.xyz - v_world);
        color = mix(color, fog.rgb, 1.0 - exp(-dist * fog.w));
        result = vec4(color, tint.a);
    }
    """;

// One triangle covering the target, (0, 0) at the top left of v_uv.
let fullscreen_vert = """
    layout(location = 0) out vec2 v_uv;
    void main() {
        vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
        v_uv = p;
        gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
    }
    """;

// Lines in world space, each vertex with a color stored as four bytes.
let line_vert = """
    layout(location = 0) in vec3 a_pos;
    layout(location = 1) in vec4 a_color;
    layout(location = 0) out vec4 v_color;
    void main() {
        v_color = a_color;
        gl_Position = view_proj * vec4(a_pos, 1.0);
    }
    """;

// The color the vertex shader gives.
let color_frag = """
    layout(location = 0) in vec4 v_color;
    layout(location = 0) out vec4 result;
    void main() { result = v_color; }
    """;

// Moves the showcase's cubes: instance 0 is the ground and stays put; the
// others circle, bob and tumble with time.
let spin_comp = """
    layout(local_size_x = 64) in;
    layout(std430, set = 1, binding = 0) buffer Instances { vec4 inst[]; };
    layout(set = 2, binding = 0) uniform Spin { float time; uint count; float pad0; float pad1; };
    void main() {
        uint k = gl_GlobalInvocationID.x;
        if (k == 0u || k >= count) return;
        float f = float(k);
        float a = time * (0.3 + 0.05 * f) + f * 2.39996;
        float r = 2.0 + mod(f * 1.7, 3.0);
        inst[k * 3u] = vec4(cos(a) * r, 0.7 + 0.4 * sin(time * 2.0 + f), sin(a) * r, 0.0);
        vec3 axis = normalize(vec3(sin(f), 1.0, cos(f * 1.3)));
        float angle = time * (1.0 + 0.1 * f);
        inst[k * 3u + 2u] = vec4(axis * sin(angle * 0.5), cos(angle * 0.5));
    }
    """;

// Sparks: each is two vec4s, position and remaining life, then velocity.
// Gravity pulls them down; a spent one respawns at the fountain with a
// hashed velocity.
let particles_comp = """
    layout(local_size_x = 64) in;
    layout(std430, set = 1, binding = 0) buffer Particles { vec4 parts[]; };
    layout(set = 2, binding = 0) uniform Sim { float dt; float time; uint count; float pad; };
    uint hash(uint x) {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }
    float rnd(uint seed) { return float(hash(seed) & 0xffffu) / 65535.0; }
    void main() {
        uint i = gl_GlobalInvocationID.x;
        if (i >= count) return;
        vec4 p = parts[i * 2u];
        vec4 v = parts[i * 2u + 1u];
        p.w -= dt;
        if (p.w <= 0.0) {
            uint s = i * 1973u + uint(time * 1000.0) * 9277u;
            p = vec4(0.0, 0.3, 0.0, 1.0 + 2.0 * rnd(s));
            v = vec4((rnd(s + 1u) - 0.5) * 2.0, 3.0 + 2.0 * rnd(s + 2u),
                     (rnd(s + 3u) - 0.5) * 2.0, 0.0);
        }
        v.y -= 4.0 * dt;
        p.xyz += v.xyz * dt;
        parts[i * 2u] = p;
        parts[i * 2u + 1u] = v;
    }
    """;

// The lit meshes seen from the light, for the shadow map: depth only.
let shadow_vert = """
    layout(location = 0) in vec3 a_pos;
    layout(location = 1) in vec3 a_normal;
    layout(location = 2) in vec2 a_uv;
    layout(std430, set = 0, binding = 0) readonly buffer Instances { vec4 inst[]; };
    layout(set = 1, binding = 1) uniform Draw { vec4 draw; };
    void main() {
        int i = (int(draw.x) + gl_InstanceIndex) * 3;
        vec3 world = inst[i].xyz + rotate_q(inst[i + 2], a_pos * inst[i + 1].xyz);
        gl_Position = light_view_proj * vec4(world, 1.0);
    }
    """;

// A depth-only pass still needs a fragment shader.
let empty_frag = """
    void main() { }
    """;

// The sky: a cube map looked up along the view ray through each pixel.
let sky_frag = """
    layout(location = 0) in vec2 v_uv;
    layout(location = 0) out vec4 result;
    layout(set = 2, binding = 0) uniform samplerCube sky;
    layout(set = 3, binding = 0) uniform View {
        vec4 right;              // scaled by tan(fov / 2) * aspect
        vec4 up;                 // scaled by tan(fov / 2)
        vec4 forward;
    };
    void main() {
        vec2 ndc = vec2(v_uv.x * 2.0 - 1.0, 1.0 - v_uv.y * 2.0);
        vec3 dir = normalize(forward.xyz + ndc.x * right.xyz + ndc.y * up.xyz);
        result = texture(sky, dir);
    }
    """;

// A camera-facing square per spark, six vertices from the vertex index and
// no vertex buffer: positions from the storage buffer the compute pass
// wrote, colors from a 3D noise texture sampled in the vertex shader.
let particle_vert = """
    layout(set = 0, binding = 0) uniform sampler3D noise;
    layout(std430, set = 0, binding = 1) readonly buffer Particles { vec4 parts[]; };
    layout(location = 0) out vec2 v_corner;
    layout(location = 1) out vec4 v_color;
    void main() {
        int corner = gl_VertexIndex % 6;
        vec2 c = vec2(corner == 1 || corner == 2 || corner == 4 ? 1.0 : -1.0,
                      corner == 2 || corner == 4 || corner == 5 ? 1.0 : -1.0);
        vec4 p = parts[gl_InstanceIndex * 2];
        float life = clamp(p.w, 0.0, 1.0);
        vec3 world = p.xyz + (cam_right.xyz * c.x + cam_up.xyz * c.y) * (0.07 * life);
        float n = textureLod(noise, fract(p.xyz * 0.25 + params.x * 0.05), 0.0).r;
        v_corner = c;
        v_color = vec4(1.0, 0.45 + 0.5 * n, 0.15 + 0.3 * n, 1.0) * life;
        gl_Position = view_proj * vec4(world, 1.0);
    }
    """;

// A soft round spark, added onto what is there.
let particle_frag = """
    layout(location = 0) in vec2 v_corner;
    layout(location = 1) in vec4 v_color;
    layout(location = 0) out vec4 result;
    void main() {
        float d = max(1.0 - dot(v_corner, v_corner), 0.0);
        result = vec4(v_color.rgb * 3.0 * d, d);
    }
    """;

// The resolved HDR image, tone mapped and gamma corrected, with a vignette.
let post_frag = """
    layout(location = 0) in vec2 v_uv;
    layout(location = 0) out vec4 result;
    layout(set = 2, binding = 0) uniform sampler2D hdr;
    layout(set = 3, binding = 0) uniform Post {
        float exposure;
        float vignette;
        float pad0;
        float pad1;
    };
    void main() {
        vec3 c = texture(hdr, v_uv).rgb * exposure;
        c = c / (1.0 + c);
        c = pow(c, vec3(1.0 / 2.2));
        vec2 d = v_uv - 0.5;
        c *= 1.0 - vignette * dot(d, d);
        result = vec4(c, 1.0);
    }
    """;

// A depth texture shown as gray, sampled plainly rather than compared.
let depthview_frag = """
    layout(location = 0) in vec2 v_uv;
    layout(location = 0) out vec4 result;
    layout(set = 2, binding = 0) uniform sampler2D depth;
    void main() {
        float d = texture(depth, v_uv).r;
        float g = clamp((1.0 - d) * 3.0, 0.0, 1.0);
        result = vec4(g, g, g, 1.0);
    }
    """;

// Screen-space squares along the bottom, one per instance, which also picks
// the icon's layer.
let icon_vert = """
    layout(location = 0) in vec2 a_corner;
    layout(set = 1, binding = 0) uniform Icons {
        vec4 place;             // x, y of the first; size; spacing
    };
    layout(location = 0) out vec3 v_uvw;
    void main() {
        vec2 p = place.xy + vec2(float(gl_InstanceIndex) * place.w, 0.0) + a_corner * place.z;
        v_uvw = vec3(a_corner.x, 1.0 - a_corner.y, float(gl_InstanceIndex));
        gl_Position = vec4(p, 0.0, 1.0);
    }
    """;

// The icon of the instance, from its layer of the texture array.
let icon_frag = """
    layout(location = 0) in vec3 v_uvw;
    layout(location = 0) out vec4 result;
    layout(set = 2, binding = 0) uniform sampler2DArray icons;
    void main() { result = texture(icons, v_uvw); }
    """;

// Flat color per draw: an object ID the showcase reads back to see what is
// on screen where.
let id_frag = """
    layout(location = 0) out vec4 result;
    layout(set = 3, binding = 0) uniform Id { vec4 id; };
    void main() { result = id; }
    """;

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
    gfx::update_texture(checker_tex, checker());
    gfx::generate_mips(checker_tex);
    let sky = gfx::texture_cube(64, gfx::RGBA8_SRGB, gfx::SAMPLED);
    for f in 6 {
        gfx::update_texture(sky, gfx::Region { layer: f as i32 }, sky_face(f, 64));
    }
    let noise = gfx::texture3d(16, 16, 16, gfx::R8, gfx::SAMPLED);
    var noise_bytes: u8[>..] = [];
    for i in 16 * 16 * 16 { noise_bytes.push(r.rand_int(256) as u8); }
    gfx::update_texture(noise, noise_bytes);
    let icons = gfx::texture_array(32, 32, 4, gfx::RGBA8, gfx::SAMPLED);
    for k in 4 {
        gfx::update_texture(icons, gfx::Region { layer: k as i32 }, icon(k));
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
    gfx::bind(gfx::compute_pipeline(embed_shader("comp", rings_comp)));
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
    let lit_vs = embed_shader("vert", scene_glsl, lit_vert);
    let lit_fs = embed_shader("frag", lit_frag);
    let full_vs = embed_shader("vert", fullscreen_vert);
    let line_vs = embed_shader("vert", scene_glsl, line_vert);
    let color_fs = embed_shader("frag", color_frag);
    let spin = gfx::compute_pipeline(embed_shader("comp", spin_comp));
    let simulate = gfx::compute_pipeline(embed_shader("comp", particles_comp));
    let shadow = gfx::pipeline(embed_shader("vert", scene_glsl, shadow_vert),
                               embed_shader("frag", empty_frag),
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
    let sky_pipe = gfx::pipeline(full_vs, embed_shader("frag", sky_frag));
    let spark_pipe = gfx::pipeline(embed_shader("vert", scene_glsl, particle_vert),
                                   embed_shader("frag", particle_frag),
                                   gfx::PipelineDesc { depth_test: true, blend: gfx::BLEND_ADD });
    var line_desc = gfx::PipelineDesc { primitive: gfx::LINES, depth_test: true,
                                        depth_write: true };
    line_desc.formats[1] = gfx::VERTEX_UBYTE4_NORM;
    let lines = gfx::pipeline(line_vs, color_fs, line_desc);
    line_desc.primitive = gfx::LINE_STRIP;
    let strip = gfx::pipeline(line_vs, color_fs, line_desc);
    let post = gfx::pipeline(full_vs, embed_shader("frag", post_frag));
    let depth_view = gfx::pipeline(full_vs, embed_shader("frag", depthview_frag));
    let icon_pipe = gfx::pipeline(embed_shader("vert", icon_vert), embed_shader("frag", icon_frag),
                                  gfx::PipelineDesc { blend: gfx::BLEND_ALPHA });
    let id_pipe = gfx::pipeline(lit_vs, embed_shader("frag", id_frag),
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
