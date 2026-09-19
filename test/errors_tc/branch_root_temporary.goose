// error: the block's value is a slice rooted at a temporary, which does not outlive it
// A temporary made in a branch's final expression lasts until the branch
// ends, not until the end of the statement around the if.
fn mk() -> i64[>..] { var a: i64[>..] = [4, 5, 6]; a.push(7); return copy(a); }
fn second(s: i64[:]) -> i64 { s[1] }
fn main() {
    var xs: i64[>..] = [1, 2, 3];
    let c = xs[0] > 0;
    print(second(if c { mk()[..] } else { xs[..] }));
}
