// error: the block's value is a slice rooted at t, which does not outlive it
// A bare block's local ends with the block, before the call its value is
// passed to.
fn second(s: i64[:]) -> i64 { s[1] }
fn main() {
    print(second({ var t: i64[>..] = [4, 5, 6]; t[..] }));
}
