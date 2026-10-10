// error: outer calls into its recursive cycle through inner, whose non-fixed-size result would land in a temporary here
// A returned call builds in the caller's destination only where its result
// type is the function's own: a variable array returned as a resizable one
// is built on a temporary first and copied (§7.8).
var all: u8[>..] = [];
recursive fn outer(n: i64) -> u8[>..] {
    if n == 0 { return "end"; }
    return inner(n - 1);
}
recursive fn inner(n: i64) -> u8[] { all.append(outer(n)); return "x"; }
fn main() { print(outer(3)); }
