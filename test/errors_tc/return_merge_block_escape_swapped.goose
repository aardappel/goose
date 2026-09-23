// error: the block's value is a slice rooted at inner, which does not outlive it
// The same as return_merge_block_escape with the arguments swapped: the
// deeper root is the earlier return's here, and still the result's (§9.2).
fn longer(a: const u8[:], b: const u8[:]) -> const u8[:] {
    if a.len >= b.len { return a; }
    return b;
}

fn main() {
    var outer: u8[>..] = "a long enough outer text";
    let r = { var inner: u8[>..] = "short"; longer(inner, outer) };
    print(r);
}
