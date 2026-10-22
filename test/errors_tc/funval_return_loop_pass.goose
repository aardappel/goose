// error: cannot clear y while r is still used
// The loop's later pass hands apply a slice rooted at b as well: the block's
// return out of it is mapped through that pass's call, not the first pass's
// alone, so pick's result may point into y.
var G: i64 = 7;
fn apply<F>(xs: i64[:]) -> i64 { F(xs); return 0; }
fn pick(a: i64[:], b: i64[:]) -> i64& {
    var cur: i64[:] = a;
    for i in 2 {
        if i == 1 { apply(cur) { s => return s[0]; }; }
        cur = b;
    }
    return G;
}
fn main() {
    var x: i64[>..] = [1, 2];
    var y: i64[>..] = [3, 4];
    let r: i64& = pick(x, y);
    y.clear();
    print(r);
}
