// error: cannot clear b while b is still used: a reference to what may hold a reference or slice into it was stored there
// The same with the shrink in the function holding the struct (§5.1).
struct H { s: const u8[:] }
struct B { h: H&, r: u8[>..] }
fn main() {
    var h = H { s: "" };
    var b = B { h: h, r: [1, 2] };
    h.s = b.r[0..1];
    b.r.clear();
    print(b.h.s);
}
