// error: storing a reference rooted at s, which does not outlive the destination
// A function handed a function value compares its arguments' roots with the
// variables the value captures: apply's first call passes main's x, which
// the block may store in hs, and its second twice's y, which dies first.
struct H { s: const u8[:] }
fn apply<F>(s: const u8[:]) { F(s); }
fn twice<F>(s: const u8[:]) {
    apply(s, F);
    var y: u8[>..] = [];
    for i in 18 { y.push('a' + i as u8); }
    apply(y[..], F);
}
fn main() {
    var hs: H[>..] = [];
    let x: u8[] = "abc";
    twice(x[..]) { hs.push(H { s: it }) }
    print(hs[1].s);
}
