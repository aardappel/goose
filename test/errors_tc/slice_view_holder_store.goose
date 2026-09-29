// error: cannot clear b while r is still used: it may hold a reference or slice into it
// k's reference names the slot p does, main's s: the store through it puts
// b's slice there (§9.2), so r may view b, which the clear frees (§5.1).
struct K { r: u8[:]& }
fn f(p: u8[:]&, k: K, o: u8[:]) -> u8[:] {
    k.r = o;
    return p[0..1];
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var b: u8[>..] = [];
    b.append("xyz");
    var s = a[..];
    let k = K { r: s };
    let r = f(s, k, b[..]);
    b.clear();
    print(r);
}
