// error: the block's value is a slice rooted at t, which does not outlive it
// A block's final expression leaves the block as a break does.
fn second(s: i64[:]) -> i64 { s[1] }
fn main() {
    var xs: i64[>..] = [1, 2, 3];
    print(second(block { var t: i64[>..] = [4, 5, 6]; if xs[0] > 5 { break xs[..]; } t[..] }));
}
