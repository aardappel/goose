// error: binding s to a slice rooted at t, which does not outlive it
// A block's value may not view a local of that block once the block ends.
fn main() {
    let s = { let t: u8[] = "abc"; t[..] };
    print(s);
}
