// error: cannot clear b while r is still used: it may hold a reference or slice into it
// The load in the loop's third iteration follows the store its second made,
// which puts b's slice in the caller's slot (§9.2): r may view b, which the
// clear frees (§5.1).
fn f(p: u8[:]&, o: u8[:], d: u8[:]) -> u8[:] {
    var i = 0;
    while i < 3 {
        if i == 2 { return p[0..1]; }
        p = o;
        i += 1;
    }
    return d;
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var b: u8[>..] = [];
    b.append("xyz");
    var s = a[..];
    let r = f(s, b[..], a[0..0]);
    b.clear();
    print(r);
}
