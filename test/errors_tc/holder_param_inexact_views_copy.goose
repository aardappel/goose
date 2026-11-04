// error: cannot clear dst while q is still used: it may hold p's views, which are not of one array exactly, so they may point into it
// q is a copy of p, whose views are of two arrays: they may point into dst,
// which this body clears while q is still used (§5.1).
struct Pair { a: const i32[:], b: const i32[:] }
fn refill(dst: i32[>..]&, p: Pair) {
    let q = p;
    dst.clear();
    for x in q.a { dst.push(x); }
}
fn main() {
    let xs: i32[] = [1, 2];
    let ys: i32[] = [3];
    var out: i32[>..] = [9];
    refill(out, Pair { a: xs, b: ys });
    print(out.len);
}
