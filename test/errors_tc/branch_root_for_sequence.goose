// error: the block's value is a slice rooted at t, which does not outlive it
// A for loop over an if's value iterates after the branch has ended.
fn main() {
    var xs: i64[>..] = [1, 2, 3];
    let c = xs[0] > 0;
    var total = 0;
    for x in (if c { var t: i64[>..] = [4, 5, 6]; t[..] } else { xs[..] }) { total += x; }
    print(total);
}
