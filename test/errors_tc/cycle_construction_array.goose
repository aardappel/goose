// error: levels calls into its recursive cycle inside a non-fixed-size array literal, which is under construction
// The literal is built in place as its elements are evaluated, so the call
// into the cycle among them runs with it under construction (§7.8): build
// the element in a statement of its own first.
var all: i64[>..] = [];
recursive fn levels(n: i64) -> i64[>..] {
    if n == 0 { return [0]; }
    return [n, count(n - 1)];
}
recursive fn count(n: i64) -> i64 { all.append(levels(n)); return all.len; }
fn main() { print(levels(3)); }
