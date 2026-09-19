// error: cannot call mk4 in the value assigned to G: mk4 uses G, and that value is built over the old contents of G
// A whole assignment builds the new contents over the old ones while the
// right-hand side runs (§4.4): the second loop would read elements the
// first one already replaced.
var G: u8[>..] = [];
fn mk4() -> u8[>..] {
    var out: u8[>..] = [];
    for x in G { out.push(x + 1); }
    for x in G { out.push(x + 2); }
    return out;
}
fn main() {
    G.append("abcdefgh");
    G = mk4();
    print(G);
}
