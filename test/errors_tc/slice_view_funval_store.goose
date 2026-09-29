// error: cannot clear b while r is still used: it may hold a reference or slice into it
// The block f hands apply stores o through f's parameter into the caller's
// slot and loads the slice back: what it loads may be anything that
// outlives the slot, b included (§9.2), which the clear frees under r
// (§5.1).
fn apply<F>() -> u8[:] { return F(); }
fn f(p: u8[:]&, o: u8[:]) -> u8[:] {
    return apply() {
        p = o;
        p[0..1]
    };
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var b: u8[>..] = [];
    b.append("xyz");
    var s = a[..];
    let r = f(s, b[..]);
    b.clear();
    print(r);
}
