// error: returning a reference rooted in scratch, which dies with this function
// A function's returns may give different roots, which its callers merge
// (§9.2), but each one must still be visible to them: a view of the
// function's own local is not, on whichever path it is returned.
fn pick(s: const u8[:], k: i64) -> const u8[:] {
    var scratch: u8[>..] = "local";
    if k > 0 { return s; }
    return scratch[..];
}

fn main() {
    var x: u8[>..] = "input";
    print(pick(x, 1));
}
