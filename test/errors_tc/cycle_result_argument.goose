// error: depth calls into its recursive cycle through depth, whose non-fixed-size result would land in a temporary here
// The result is built where the call's destination is: as an argument of
// another call, that is a temporary taken before the call and held across
// it (§7.8). Returned whole, it is built in the caller's destination.
fn size(s: const u8[:]) -> i64 { s.len }
recursive fn depth(n: i64) -> u8[>..] {
    if n == 0 { return "end"; }
    let below = size(depth(n - 1));
    return str(below);
}
fn main() { print(depth(3)); }
