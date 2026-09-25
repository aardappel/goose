// error: operator + cannot be applied to i64? and i64
// A recursive call may rebind what a `.=` names in a function it reaches
// through a function value: the cycle's second round applies the rebind at
// the back edge, so the read after it cannot rely on g.
var n = 41;
var g: i64? = null;
fn clear() { g .= null; }
fn apply<F>() { F(); }
recursive fn r(k: i64) {
    if k > 0 { r(k - 1); }
    print(g + 1);
    apply(clear);
}
fn main() {
    g .= n;
    r(1);
}
