// error: cannot call reset, which shrinks s while h is still used: a reference into it was stored there
// A holder of a reference into the tail stops its assignment through a
// reference while the holder is used (§5.1).
struct S { n: i64, items: i64[>..] }
struct H { r: i64& }
fn reset(s: S&) { s.items = []; }
fn main() {
    var s = S { n: 3, items: [1, 2] };
    var h = H { r: s.items[0] };
    reset(s);
    print(h.r);
}
