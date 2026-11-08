// error: cannot call h: it shrinks outer while p is still used
// outer is a local of main, not of h, which clears it: p, which h is passed
// a view of outer in, is still used after the clear (§5.1).
struct P { s: const u8[:] }
fn main() {
    var outer: u8[>..] = [5];
    fn h(p: P) -> i64 { outer.clear(); return p.s.len; }
    print(h(P { s: outer[..] }));
}
