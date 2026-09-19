// error: the block's value is a reference rooted at t, which does not outlive it
// The branch's local is gone before the function returns the reference.
fn first(xs: i64[>..]&) -> i64& {
    if xs.len > 0 { xs[0] } else { var t: i64[>..] = [4]; t[0] }
}
fn main() {
    var xs: i64[>..] = [1, 2, 3];
    print(first(xs));
}
