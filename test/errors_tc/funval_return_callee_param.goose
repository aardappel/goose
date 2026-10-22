// error: cannot clear a while r is still used
// A block's return out of the function it is handed to is rooted where that
// call's argument is, as the function's own result would be: s[1] points
// into pick's ys, so pick's result may point into a, which the clear frees.
var G: i64 = 7;
fn apply<F>(xs: i64[:]) -> i64 { F(xs); return 0; }
fn pick(ys: i64[:]) -> i64& {
    apply(ys) { s => return s[1]; };
    return G;
}
fn main() {
    var a: i64[>..] = [5, 6, 7, 8];
    let r: i64& = pick(a);
    a.clear();
    print(r);
}
