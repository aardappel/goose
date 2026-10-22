// error: cannot clear GARR while r is still used
// Each round of walk's cycle records its returns anew: the block's return out
// of apply is checked again in each, not left out of the rounds after the
// first, so walk's result may point into GARR.
var GARR: i64[>..] = [1, 2, 3];
var G: i64 = 7;
fn apply<F>() -> i64 { F(0); return 0; }
recursive fn walk(n: i64, xs: i64[>..]&) -> i64& {
    if n > 0 { walk(n - 1, xs); }
    if n == 0 { apply() { k => return xs[k]; }; }
    return G;
}
fn main() {
    let r: i64& = walk(0, GARR);
    GARR.clear();
    print(r);
}
