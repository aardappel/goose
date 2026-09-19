// error: binding s to a slice rooted at a temporary, which does not outlive it
// A temporary made inside a branch ends with the branch.
fn mk() -> u8[] { "hello" }
fn main() {
    let c = true;
    let s = if c { mk()[1..] } else { "x" };
    print(s);
}
