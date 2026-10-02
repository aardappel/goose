// error: cannot call g: it may shrink a while ys[..] is still used, and ys[..] may refer into a
// The same for grow-only arrays: the pair is the callers' to judge (§5.1,
// §6.5, §9.5).
struct H { a: i64[>..]& }
fn g(xs: i64[>..]&, ys: i64[>..]&) {
    for y in ys[..] {
        xs.pop();
        xs.push(5);
        print(y);
    }
}
fn main() {
    var a: i64[>..] = [1, 2, 3];
    var b: i64[>..] = [4];
    let hs = [H { a: b }];
    let h = hs[0];
    g(h.a, a);
}
