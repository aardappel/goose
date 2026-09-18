// embed_shader takes a shader file, or the shader's GLSL written in the
// program, in parts if need be: however it is written, one shader compiles
// to one blob. Nothing here needs a GPU, only the shader compiler.
import std;

// Shader code as a global, which embed_shader calls can name.
let doubled_glsl = """
    float doubled(float x) { return x * 2.0; }
    """;
let doubled_too = doubled_glsl;

// A blob's magic and stage: 0 vertex, 1 fragment, 2 compute.
fn describe(what: const u8[:], blob: const u8[:]) {
    print(what, ": ", blob[0..4], " stage ", blob[6]);
}

fn main() {
    // probe.frag, which includes probe_common.glsl, and the same shader
    // written here, including it relative to this file.
    let from_file = embed_shader("probe.frag");
    let written = embed_shader("frag", """
        #include "probe_common.glsl"
        layout(location = 0) in vec2 v_uv;
        layout(location = 0) out vec4 result;
        layout(set = 2, binding = 0) uniform sampler2D tex;
        layout(set = 3, binding = 0) uniform Params { vec4 tint; float gain; };
        void main() { result = texture(tex, v_uv) * tint * doubled(gain); }
        """);
    describe("from a file", from_file);
    print("written here: the same blob ", written == from_file);

    // The parts of a shader are its lines in order: doubled() from a global
    // does what the include did.
    let parts = embed_shader("frag", doubled_glsl, """
        layout(location = 0) in vec2 v_uv;
        layout(location = 0) out vec4 result;
        layout(set = 2, binding = 0) uniform sampler2D tex;
        layout(set = 3, binding = 0) uniform Params { vec4 tint; float gain; };
        void main() { result = texture(tex, v_uv) * tint * doubled(gain); }
        """);
    print("in parts: the same blob ", parts == from_file);

    // A global naming another stands for the same text, and a plain string
    // literal is source too.
    let chained = embed_shader(
        "frag", doubled_too,
        "layout(location = 0) out vec4 result; void main() { result = vec4(doubled(0.5)); }");
    describe("through a second global", chained);

    // The other two stages.
    describe("vertex", embed_shader("vert", """
        layout(location = 0) in vec3 a_pos;
        void main() { gl_Position = vec4(a_pos, 1.0); }
        """));
    let comp = embed_shader("comp", """
        layout(local_size_x = 8, local_size_y = 4) in;
        void main() { }
        """);
    describe("compute", comp);
    print("local size ", comp[8], " x ", comp[12], " x ", comp[16]);
}
