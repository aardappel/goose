// error: cannot clear buf while q is still used: a reference into it was stored there
// What p was passed cannot view buf, which f declares itself, but q, a copy
// of p, is then given a view of buf, which the clear frees while q is still
// used (§5.1).
struct P { s: const i32[:] }
fn f(p: P) -> i64 {
    var q = p;
    var buf: i32[>..] = [1];
    q.s = buf[..];
    buf.clear();
    return q.s.len;
}
fn main() { print(f(P { s: [1, 2] })); }
