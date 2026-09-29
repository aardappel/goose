// error: returning a reference rooted in t, which dies with this function
// The back edge's slot holds a slice of buf, which the entry call's did not:
// what a recursive fn loads through its parameter is bounded by the slot,
// whatever slice it held at the entry call (§7.8, §9.2).
recursive fn f(p: const u8[:]&, n: i64) -> const u8[:] {
    if n == 0 { return p[0..1]; }
    var buf: u8[4] = [65, 66, 67, 68];
    var t: const u8[:] = buf[..];
    return f(t, n - 1);
}
fn main() {
    var s: const u8[:] = "xyz";
    print(f(s, 2));
}
