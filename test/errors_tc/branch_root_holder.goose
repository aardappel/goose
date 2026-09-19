// error: the block's value holds references rooted at t, which does not outlive it
// A struct holding a slice of a branch's local is a copy that still points
// into the local.
struct View { s: i64[:] }
fn second(v: View) -> i64 { v.s[1] }
fn main() {
    var xs: i64[>..] = [1, 2, 3];
    let c = xs[0] > 0;
    print(second(if c { var t: i64[>..] = [4, 5, 6]; View { s: t[..] } } else { View { s: xs[..] } }));
}
