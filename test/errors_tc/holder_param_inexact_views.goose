// error: cannot clear dst while p is still used: p's views are not of one array exactly, so they may point into it
// p's views are of two arrays, so the call cannot pass it as views of one
// array exactly, and its class only bounds them: they may point into dst,
// which this body clears while p is still used (§5.1). No store in the
// body put them there.
struct Pair { a: const i32[:], b: const i32[:] }
fn refill(dst: i32[>..]&, p: Pair) {
    dst.clear();
    for x in p.a { dst.push(x); }
    for x in p.b { dst.push(x); }
}
fn main() {
    let xs: i32[] = [1, 2];
    let ys: i32[] = [3];
    var out: i32[>..] = [9];
    refill(out, Pair { a: xs, b: ys });
    print(out.len);
}
