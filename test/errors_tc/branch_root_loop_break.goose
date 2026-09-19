// error: the loop's value is a slice rooted at t, which does not outlive it
// A local of the loop's body ends with the iteration that breaks out.
fn second(s: i64[:]) -> i64 { s[1] }
fn main() {
    print(second(loop { var t: i64[>..] = [4, 5, 6]; break t[..]; }));
}
