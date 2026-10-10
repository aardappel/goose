// error: cannot clear t while h is still used: a reference into it was stored there
// A holder that a reference into a tail field was stored into stops the
// field's shrink while it is used (§5.1).
struct S { n: i64, a: u8[>..] }
struct H { r: u8& }
fn main() {
    var t = S { n: 1, a: [1, 2, 3] };
    var h = H { r: t.a[0] };
    t.a.clear();
    print(h.r);
}
