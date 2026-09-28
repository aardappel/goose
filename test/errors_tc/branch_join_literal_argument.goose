// error: the block's value is a slice rooted at a temporary, which does not outlive it
// At a slice parameter an array literal in a branch is a temporary of that
// branch too, although the call views it only while it runs (§6.4, §9.2).
fn total(xs: i64[:]) -> i64 {
    var t = 0;
    for x in xs { t += x; }
    t
}
fn main() {
    let xs: i64[3] = [1, 2, 3];
    let c = xs[0] > 0;
    print(total(if c { xs } else { [4, 5] }));
}
