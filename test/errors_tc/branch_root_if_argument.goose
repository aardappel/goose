// error: the block's value is a slice rooted at t, which does not outlive it
// An if's branch ends before the call its value is passed to begins, and
// the branch's local ends with it, although an argument is no store (§9.2).
fn second(s: i64[:]) -> i64 { s[1] }
fn main() {
    var xs: i64[>..] = [1, 2, 3];
    let c = xs[0] > 0;
    print(second(if c { var t: i64[>..] = [4, 5, 6]; t[..] } else { xs[..] }));
}
