// error: cannot clear b (which r may point at) while h is still used: a reference into it was stored there
// The store record names b, and clearing through r may clear b (§5.1).
struct H { s: const i64[:] }
fn main() {
    var a: i64[>..] = [1, 2, 3];
    var b: i64[>..] = [4, 5];
    let c = a.len < b.len;
    let h = H { s: b[..] };
    let r: i64[>..]& = if c { a } else { b };
    r.clear();
    b.push(77);
    b.push(78);
    print(h.s[1]);
}
