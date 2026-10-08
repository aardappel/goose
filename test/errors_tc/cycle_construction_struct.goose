// error: build calls into its recursive cycle inside a non-fixed-size struct literal, which is under construction
// A struct literal of variable size is built in place, field by field, so
// the call into the cycle in a field's initializer runs with it under
// construction (§7.8).
struct Node { depth: i64, name: u8[] }
fn total(s: const Node[:]) -> i64 { var t = 0; for n in s { t += n.depth; } return t; }
recursive fn build(n: i64) -> i64 {
    if n == 0 { return 0; }
    return total([Node { depth: build(n - 1), name: "node" }]);
}
fn main() { print(build(3)); }
