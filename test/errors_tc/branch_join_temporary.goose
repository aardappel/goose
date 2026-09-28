// error: the block's value is a slice rooted at a temporary, which does not outlive it
// A call's array result is a temporary of the branch that makes it, which a
// slice of it would outlive (§6.4, §9.2).
fn made() -> u8[..8] { "made" }
fn main() {
    let c = made().len > 3;
    let s = if c { "none" } else { made() };
    print(s);
}
