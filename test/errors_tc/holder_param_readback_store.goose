// error: storing a reference rooted at w, which does not outlive the destination
// put keeps what its by-value parameter's field points at in an array of the
// caller's: the holder class that bounds it stands for inner's y here, which
// dies before main's a does (§9.2, §9.5).
struct H { s: const u8[:] }
struct W { s: const u8[:] }
fn put(h: H[>..]&, w: W) { h.push(H { s: w.s }); }
fn inner(a: H[>..]&) {
    var y: u8[>..] = [];
    for i in 18 { y.push('a' + i as u8); }
    let w = W { s: y[..] };
    put(a, w);
}
fn main() { var a: H[>..] = []; inner(a); print(a[0].s); }
