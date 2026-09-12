// A recursive call may rebind what a `.=` names anywhere its code can reach,
// here only through function values and functions passed on by name: the
// read after the back edge cannot rely on g.
var n = 41;
var g: i64? = null;
fn clear() { g .= null; }
fn apply<F>() { F(); }
fn relay() { apply(clear); }
fn pass() { relay.apply(); }
fn outer<H>() {
    recursive fn r(k: i64) {
        if k > 0 { r(k - 1); }
        print(g + 1);
        H();
    }
    r(1);
}
fn outer2<K>() { outer() { K(); } }
fn main() {
    g .= n;
    outer2(pass);
}
