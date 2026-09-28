// error: branches have mismatched types: const u8[:] vs i64[3]
// Only arrays and slices of one element type join as a slice (§6.4).
fn main() {
    let ns: i64[3] = [1, 2, 3];
    let c = ns[0] > 0;
    let s = if c { "x" } else { ns };
    print(s);
}
