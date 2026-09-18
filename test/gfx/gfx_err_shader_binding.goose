// A resource outside the set SDL_GPU gives it is caught when the program is
// compiled, not on whichever backend would bind it wrongly.
fn main() {
    let fs = embed_shader("probe_badset.frag");
    print(fs.len);
}
// error: fragment shader sampler 'tex' is in set 0, and must be in set 2
