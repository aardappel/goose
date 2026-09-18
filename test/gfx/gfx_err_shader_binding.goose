// A resource outside the set SDL_GPU gives it is caught when the program is
// compiled, not on whichever backend would bind it wrongly.
fn main() {
    let fs = embed_shader("frag", """
        layout(location = 0) out vec4 result;
        layout(set = 0, binding = 0) uniform sampler2D tex;
        void main() { result = texture(tex, vec2(0.5)); }
        """);
    print(fs.len);
}
// error: fragment shader sampler 'tex' is in set 0, and must be in set 2
