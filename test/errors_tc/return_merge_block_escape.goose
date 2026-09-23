// error: the block's value is a slice rooted at inner, which does not outlive it
// A call's result is rooted at the deepest of the roots its callee's returns
// give, mapped to its arguments (§9.2): here the later return's, `inner`,
// whichever return the call takes at run time, so the view may not leave
// the block that declares `inner`.
fn longer(a: const u8[:], b: const u8[:]) -> const u8[:] {
    if a.len >= b.len { return a; }
    return b;
}

fn main() {
    var outer: u8[>..] = "a long enough outer text";
    let r = { var inner: u8[>..] = "short"; longer(outer, inner) };
    print(r);
}
