// error: depth calls into its recursive cycle through depth, whose non-fixed-size result would land in a temporary here
// A result nothing receives still lands in a temporary of the statement,
// released at its end (§7.8): a cycle function whose result is not wanted
// takes an output buffer, or returns nothing.
recursive fn depth(n: i64) -> u8[>..] {
    if n == 0 { return "end"; }
    depth(n - 1);
    return str(n);
}
fn main() { print(depth(3)); }
