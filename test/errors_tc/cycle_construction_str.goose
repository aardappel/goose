// error: show calls into its recursive cycle inside the arguments of str(...), whose text is under construction
// str renders its arguments one by one into the text it builds, so the call
// into the cycle among them runs with that text under construction (§7.8):
// render its result in a statement of its own first, or append in place into
// a buffer passed in from outside the cycle.
var shown: u8[>..] = [];
recursive fn show(n: i64) -> u8[>..] {
    if n == 0 { return str("end"); }
    return str("<", count(n - 1), ">");
}
recursive fn count(n: i64) -> i64 { shown.append(show(n)); return shown.len; }
fn main() { print(show(3)); }
