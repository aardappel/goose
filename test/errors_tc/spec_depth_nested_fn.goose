// error: storing a reference rooted at s, which does not outlive the destination
// A nested function also compares its arguments' roots with the variables it
// captures: keep may store x, declared beside hs, in hs, but not the inner
// block's y.
struct H { s: const u8[:] }
fn main() {
    var hs: H[>..] = [];
    fn keep(s: const u8[:]) { hs.push(H { s: s }); }
    let x: u8[] = "abc";
    keep(x[..]);
    {
        var y: u8[>..] = [];
        for i in 18 { y.push('a' + i as u8); }
        keep(y[..]);
    }
    print(hs[1].s);
}
