// error: cannot write through this reference: non-writable provenance
// A field of a call's result lies in a temporary, which a write would change
// and nothing else (§9.2, §9.5): bound by reference, as `&mk().s` of it is,
// it is a read-only reference.
struct H { s: u8[:] }
var g: u8[>..] = [];
fn mk() -> H { return H { s: g[0..2] }; }
fn adv(p: u8[:]&) { p = p[1..]; }
fn main() {
    g.append("abc");
    adv(mk().s);
    print(g);
}
