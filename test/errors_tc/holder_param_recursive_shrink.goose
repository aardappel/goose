// error: cannot pop xs while w is still used
// A recursive function's back edges reuse its body whatever they pass (§7.8),
// so what its by-value parameter holds is never one array there: the entry
// call's w views ys alone, but the back edge's views xs, which the body pops
// while w is still used (§9.2, §5.1).
struct W { s: i64[:] }
recursive fn walk(xs: i64[>..]&, w: W, n: i64) {
    if n == 0 { return; }
    xs.pop();
    xs.push(70 + n);
    print(w.s[w.s.len - 1]);
    walk(xs, W { s: xs[..] }, n - 1);
}
fn main() {
    var xs: i64[>..] = [1, 2, 3];
    var ys: i64[>..] = [4, 5];
    walk(xs, W { s: ys[..] }, 2);
}
