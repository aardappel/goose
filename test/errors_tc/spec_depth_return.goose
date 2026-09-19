// error: storing a reference rooted at y, which does not outlive the destination
// pick's result is rooted at the deeper of its arguments: a where they share
// a depth, as main's x and z do, b where b is deeper, as inner's y is. Its
// body checked for main's call does not say which one inner's result points
// into.
struct H { s: const u8[:] }
fn pick(a: const u8[:], b: const u8[:], c: bool) -> const u8[:] { if c { a } else { b } }
fn inner(h: H[>..]&, x: const u8[:]) {
    var y: u8[>..] = [];
    for i in 18 { y.push('a' + i as u8); }
    h.push(H { s: pick(x, y[..], false) });
}
fn main() {
    var hs: H[>..] = [];
    let x: u8[] = "abc";
    let z: u8[] = "xyz";
    hs.push(H { s: pick(x[..], z[..], true) });
    inner(hs, x[..]);
    print(hs[1].s);
}
