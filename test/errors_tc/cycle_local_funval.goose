// error: through_block calls into its recursive cycle while non-fixed-size local tmp
// A function value's body runs inside the call that calls it: its locals
// are in scope at the calls it makes into the cycle (§7.8).
fn apply<F>(x: i64) -> i64 { return F(x); }
recursive fn through_block(n: i64) -> i64 {
    if n == 0 { return 0; }
    return apply(n) { k =>
        var tmp: u8[>..] = [];
        tmp.append("xyz");
        through_block(k - 1) + tmp.len
    };
}
fn main() { print(through_block(3)); }
