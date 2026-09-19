// error: the block's value is a slice rooted at t, which does not outlive it
// A fixed-size local ends with its block as well, wherever its storage lies.
fn second(s: i64[:]) -> i64 { s[1] }
fn main() {
    var xs: i64[3] = [1, 2, 3];
    let c = xs[0] > 0;
    print(second(if c { var t: i64[3] = [4, 5, 6]; t[..] } else { xs[..] }));
}
