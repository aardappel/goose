// error: storing a reference rooted at s, which does not outlive the destination
// A specialization checked where its arguments' roots share a depth serves
// only calls where they share one again: put may keep main's x in a, which
// lives as long, but not inner's y, which dies first.
struct H { s: const u8[:] }
fn put(h: H[>..]&, s: const u8[:]) { h.push(H { s: s }); }
fn inner(a: H[>..]&) {
    var y: u8[>..] = [];
    for i in 18 { y.push('a' + i as u8); }
    put(a, y[..]);
}
fn main() {
    var a: H[>..] = [];
    let x: u8[] = "abc";
    put(a, x[..]);
    inner(a);
    print(a[1].s);
}
