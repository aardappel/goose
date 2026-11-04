// error: cannot clear dst while p is still used
// p views out, which the call also passes as dst: clearing dst frees what
// p.s views while p is still used (§5.1).
struct P { s: const i32[:] }
fn f(dst: i32[>..]&, p: P) -> i64 {
    dst.clear();
    return p.s.len;
}
fn main() {
    var out: i32[>..] = [1, 2, 3];
    print(f(out, P { s: out[..] }));
}
