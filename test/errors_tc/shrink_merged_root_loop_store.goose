// error: cannot clear b (which r may point at) while h is in scope: a reference into it is stored there
// error: , which the next iteration reaches
// The store into h comes after the clear through r, which may clear b, so the
// next iteration's clear would invalidate it (§5.1).
struct H { s: const i64[:] }
fn main() {
    var a: i64[>..] = [1, 2, 3];
    var b: i64[>..] = [4, 5];
    let z: i64[3] = [0, 0, 0];
    let c = a.len < b.len;
    var h = H { s: z[..0] };
    let r: i64[>..]& = if c { a } else { b };
    for i in 0..2 {
        r.clear();
        h = H { s: b[..] };
    }
    print(h.s.len);
}
