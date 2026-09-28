// error: cannot call drop, which shrinks a while w is still used: it may hold a reference or slice into it
// A literal's later fields are built after the one that clears the array
// (§5.1).
fn drop(a: i64[>..]&) -> i64 { a.clear(); return 0; }
struct U { n: i64, s: const i64[:] }
fn main() {
    var a: i64[>..] = [1, 2];
    let w = a[..];
    let u = U { n: drop(a), s: w };
    print(u.s);
}
