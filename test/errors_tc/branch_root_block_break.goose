// error: the block's value is a slice rooted at t, which does not outlive it
// A break leaves the block that declares t.
fn second(s: i64[:]) -> i64 { s[1] }
fn main() {
    var xs: i64[>..] = [1, 2, 3];
    print(second(block { var t: i64[>..] = [4, 5, 6]; if xs[0] > 0 { break t[..]; } xs[..] }));
}
