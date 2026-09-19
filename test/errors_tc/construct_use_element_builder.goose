// error: cannot call mk3 in the value assigned to G: mk3 uses G
// The second element's builder would read the elements whose bytes the
// first element already replaced (§4.4).
var G: u8[][>..] = [];
fn mk3() -> u8[] {
    var n = 0;
    for s in G { n += s.len; }
    let out: u8[] = [(n + 96) as u8; 3];
    return out;
}
fn main() {
    G.push("ab");
    G.push("cdef");
    G = [mk3(), mk3()];
    print(G);
}
