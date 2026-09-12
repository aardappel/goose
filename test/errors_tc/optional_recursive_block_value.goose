// A recursive call may rebind what a `.=` names in a function that reaches a
// call as a block's value rather than as an argument: the read after the back
// edge cannot rely on g.
var n = 41;
var g: i64? = null;
fn clear() { g .= null; }
fn apply<F>() { F(); }
recursive fn r(k: i64) {
    if k > 0 { r(k - 1); }
    print(g + 1);
    apply(block { break clear; });
}
fn main() {
    g .= n;
    r(1);
}
