// error: cannot call clr, which shrinks a: rr, evaluated earlier in the statement, may still refer into a
// use2 is handed a reference to rr before clr runs, and reads through rr's
// field and s after it (§5.1).
struct R { r: u8[:]& }
fn clr(a: u8[>..]&) -> i64 {
    a.clear();
    a.append("ZZZZZZZZ");
    return 0;
}
fn use2(h: R&, x: i64) { print(h.r); }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var s = a[..];
    var rr = R { r: s };
    use2(rr, clr(a));
}
