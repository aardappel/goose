// Textures with the gfx module, headless: 2D, 3D, cube and array textures,
// uploaded whole and by region and read back; sampled by shaders at texel
// centers with nearest filtering, so exactly; mips generated and read; float
// texels; and rendering into one face of a cube.
import std;
import vec;
import os;
import gfx;

struct Lod { lod: f32, pad 12 }
struct Slice { w: f32, pad 12 }

fn open_or_skip() -> bool {
    if gfx::open_headless(64, 64) { return true; }
    write_stderr(str("gfx: ", gfx::error(), "\n"));
    return false;
}

fn texels(px: u8[:]) -> u8[>..] {
    var out: u8[>..] = [];
    for i in px.len / 4 {
        if i > 0 { out.push(' '); }
        format(out, px[i * 4], ",", px[i * 4 + 1], ",", px[i * 4 + 2], ",", px[i * 4 + 3]);
    }
    return out;
}

fn same(a: u8[:], b: u8[:]) -> bool {
    if a.len != b.len { return false; }
    for i in a.len {
        if a[i] != b[i] { return false; }
    }
    return true;
}

fn main() {
    guard open_or_skip() else { return; }
    let nearest = gfx::sampler(gfx::NEAREST, gfx::CLAMP);
    // One triangle covering the target, with no vertex buffer: its texture
    // coordinates run 0 to 1 over the target, (0, 0) at the top left.
    let full_vs = embed_shader("vert", """
        layout(location = 0) out vec2 v_uv;
        void main() {
            vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
            v_uv = p;
            gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
        }
        """);

    // 2D: a 4 x 4 pattern up and back, then a 2 x 2 region of it replaced.
    var pattern: u8[>..] = [];
    for i in 16 { pattern.append([(i * 16) as u8, (255 - i * 16) as u8, (i * 3) as u8, 255]); }
    let tex = gfx::texture2d(4, 4);
    gfx::update_texture(tex, pattern);
    let back = gfx::read_pixels(tex);
    print("2d: read back as written ", same(back, pattern));
    let white: u8[16] = [255; 16];
    gfx::update_texture(tex, gfx::Region { x: 1, y: 1, w: 2, h: 2 }, white);
    let patched = gfx::read_pixels(tex);
    print("2d: row 1 after a region update ", texels(patched[16..32]));

    // Sampling it into a target of its size, texel for texel, at the mip
    // level a uniform block gives.
    let sample2d = embed_shader("frag", """
        layout(location = 0) in vec2 v_uv;
        layout(location = 0) out vec4 result;
        layout(set = 2, binding = 0) uniform sampler2D tex;
        layout(set = 3, binding = 0) uniform Lod { float lod; };
        void main() { result = textureLod(tex, v_uv, lod); }
        """);
    let target = gfx::render_target(4, 4, gfx::RGBA8);
    let copy = gfx::pipeline(full_vs, sample2d);
    gfx::begin_pass(target, gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 0.0 });
    gfx::bind(copy);
    gfx::bind_texture(gfx::FRAGMENT, 0, tex, nearest);
    gfx::uniforms(gfx::FRAGMENT, 0, Lod { lod: 0.0 });
    gfx::draw(3);
    gfx::end_pass();
    print("2d: sampled as written ", same(gfx::read_pixels(target), patched));

    // Mips: four quadrants of 8 x 8, averaged down to the one texel of mip 3,
    // read back directly and sampled.
    let mipped = gfx::texture(gfx::TextureDesc { width: 8, height: 8, mips: 0 });
    var quads: u8[>..] = [];
    for y in 8 {
        for x in 8 {
            let c: u8[4] = if y < 4 { if x < 4 { [255, 0, 0, 255] } else { [0, 255, 0, 255] } }
                           else { if x < 4 { [0, 0, 255, 255] } else { [255, 255, 255, 255] } };
            quads.append(c);
        }
    }
    gfx::update_texture(mipped, quads);
    gfx::generate_mips(mipped);
    let info = gfx::texture_info(mipped);
    print("mips: ", info.width, " x ", info.height, " with ", info.mips, " levels");
    var last: u8[4] = [0; 4];
    gfx::read_texture(mipped, gfx::Region { mip: 3 }, last);
    let one = gfx::render_target(1, 1, gfx::RGBA8);
    gfx::begin_pass(one, gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 0.0 });
    gfx::bind(copy);
    gfx::bind_texture(gfx::FRAGMENT, 0, mipped, gfx::sampler(gfx::LINEAR, gfx::CLAMP));
    gfx::uniforms(gfx::FRAGMENT, 0, Lod { lod: 3.0 });
    gfx::draw(3);
    gfx::end_pass();
    let sampled = gfx::read_pixels(one);
    // Averaging rounds either way, depending on the GPU.
    var near = true;
    for i in 3 { near = near && abs(last[i] as i64 - 128) <= 2 && abs(sampled[i] as i64 - 128) <= 2; }
    print("mips: level 3 averages the quadrants ", near, ", alpha ", last[3], " ", sampled[3]);

    // 3D: a 2 x 2 x 2 volume, read back, and each slice sampled.
    var vol: u8[>..] = [];
    for i in 8 { vol.append([(i * 30) as u8, (i * 5) as u8, 200, 255]); }
    let volume = gfx::texture3d(2, 2, 2, gfx::RGBA8, gfx::SAMPLED);
    gfx::update_texture(volume, vol);
    print("3d: read back as written ", same(gfx::read_pixels(volume), vol));
    // One slice of a volume, by depth coordinate.
    let sample3d = embed_shader("frag", """
        layout(location = 0) in vec2 v_uv;
        layout(location = 0) out vec4 result;
        layout(set = 2, binding = 0) uniform sampler3D volume;
        layout(set = 3, binding = 0) uniform Slice { float w; };
        void main() { result = texture(volume, vec3(v_uv, w)); }
        """);
    let slice_target = gfx::render_target(2, 2, gfx::RGBA8);
    let slicer = gfx::pipeline(full_vs, sample3d);
    for z in 2 {
        gfx::begin_pass(slice_target, gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 0.0 });
        gfx::bind(slicer);
        gfx::bind_texture(gfx::FRAGMENT, 0, volume, nearest);
        gfx::uniforms(gfx::FRAGMENT, 0, Slice { w: (z as f32 + 0.5) / 2.0 });
        gfx::draw(3);
        gfx::end_pass();
        print("3d: slice ", z, " sampled as written ",
              same(gfx::read_pixels(slice_target), vol[z * 16..z * 16 + 16]));
    }

    // Cube: one color per face, each uploaded as its own layer, then looked
    // up along each axis.
    let faces: u8[4][6] = [[255, 0, 0, 255], [0, 255, 0, 255], [0, 0, 255, 255],
                           [255, 255, 0, 255], [0, 255, 255, 255], [255, 0, 255, 255]];
    let sky = gfx::texture_cube(1, gfx::RGBA8, gfx::SAMPLED);
    for f in 6 { gfx::update_texture(sky, gfx::Region { layer: f as i32 }, faces[f]); }
    // Pixel x of a 6 x 1 target looks along cube face x's axis: +x, -x, +y,
    // -y, +z, -z.
    let along_axes = embed_shader("frag", """
        layout(location = 0) out vec4 result;
        layout(set = 2, binding = 0) uniform samplerCube sky;
        void main() {
            int face = int(gl_FragCoord.x);
            vec3 dir = vec3(1.0, 0.0, 0.0);
            if (face == 1) dir = vec3(-1.0, 0.0, 0.0);
            if (face == 2) dir = vec3(0.0, 1.0, 0.0);
            if (face == 3) dir = vec3(0.0, -1.0, 0.0);
            if (face == 4) dir = vec3(0.0, 0.0, 1.0);
            if (face == 5) dir = vec3(0.0, 0.0, -1.0);
            result = texture(sky, dir);
        }
        """);
    let axes = gfx::render_target(6, 1, gfx::RGBA8);
    gfx::begin_pass(axes, gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 0.0 });
    gfx::bind(gfx::pipeline(full_vs, along_axes));
    gfx::bind_texture(gfx::FRAGMENT, 0, sky, nearest);
    gfx::draw(3);
    gfx::end_pass();
    print("cube: +x -x +y -y +z -z ", texels(gfx::read_pixels(axes)));

    // An array: one color per layer, each layer sampled.
    let layered = gfx::texture_array(1, 1, 3, gfx::RGBA8, gfx::SAMPLED);
    for l in 3 { gfx::update_texture(layered, gfx::Region { layer: l as i32 }, faces[l + 3]); }
    // Pixel x of an N x 1 target samples layer x.
    let by_layer = embed_shader("frag", """
        layout(location = 0) out vec4 result;
        layout(set = 2, binding = 0) uniform sampler2DArray layers;
        void main() { result = texture(layers, vec3(0.5, 0.5, floor(gl_FragCoord.x))); }
        """);
    let row = gfx::render_target(3, 1, gfx::RGBA8);
    gfx::begin_pass(row, gfx::Texture { 0 }, float4 { 0.0, 0.0, 0.0, 0.0 });
    gfx::bind(gfx::pipeline(full_vs, by_layer));
    gfx::bind_texture(gfx::FRAGMENT, 0, layered, nearest);
    gfx::draw(3);
    gfx::end_pass();
    print("array: layers 0 1 2 ", texels(gfx::read_pixels(row)));

    // Rendering into one face of a cube: clearing face 4.
    let env = gfx::texture_cube(2, gfx::RGBA8, gfx::COLOR_TARGET | gfx::SAMPLED);
    var face_pass = gfx::PassDesc { layer: 4, color_value: float4 { 1.0, 0.2, 0.0, 1.0 } };
    face_pass.color[0] = env;
    gfx::begin_pass(face_pass);
    gfx::end_pass();
    var face4: u8[16] = [0; 16];
    gfx::read_texture(env, gfx::Region { layer: 4 }, face4);
    print("cube face 4 cleared: ", texels(face4));

    // Float texels, exactly.
    let hdr = gfx::texture2d(2, 1, gfx::RGBA32F, gfx::SAMPLED);
    let values: float4[2] = [float4 { 0.5, -1.25, 1000.0, 1.0 }, float4 { 3.0, 0.0, -0.125, 2.0 }];
    gfx::update_texture(hdr, bytes_of(values));
    var floats: float4[>..] = [];
    gfx::read_texture(hdr, floats);
    print("float texels: ", floats[0], " ", floats[1]);
    gfx::close();
}
