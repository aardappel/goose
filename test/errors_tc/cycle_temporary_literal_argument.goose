// error: walk calls into its recursive cycle with the non-fixed-size temporary [Tag { name: str(n) }] as an argument
// A literal of non-fixed-size class viewed by a slice parameter is a
// temporary of the statement, held across the call into the cycle (§7.8). A
// fixed-size literal is a native value, which costs no data stack.
struct Tag { name: u8[] }
recursive fn walk(s: const Tag[:], n: i64) -> i64 {
    if n == 0 { return 0; }
    var total = 0;
    for t in s { total += t.name.len; }
    return total + walk([Tag { name: str(n) }], n - 1);
}
fn main() { print(walk([Tag { name: "x" }], 3)); }
