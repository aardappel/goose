// A while condition runs again after the body: a call in it that rebinds
// the optional through a function value breaks the narrowing its earlier
// read relied on.
fn apply<F>() -> bool { F(); true }
var n = 41;
var opt: i64? = null;
fn clear() { opt .= null; }
fn main() {
    opt .= n;
    var i = 0;
    while opt + i < 43 && apply(clear) { i++; }
    print(i);
}
