// error: cannot call g: it shrinks GA while h is still used, and h may refer into GA
// g copies a holder out of the elements hs views, whose element views GA,
// which g clears (§5.1).
struct H { s: const u8[:] }
var GA: u8[>..] = [];
fn g(hs: H[:]) {
    let h = hs[0];
    GA.clear();
    GA.append("ZZZZZZZZ");
    print(h.s);
}
fn main() {
    GA.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: GA[..] });
    g(hs[..]);
}
