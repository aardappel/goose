// error: branches have mismatched types: i64[3] vs i64[2]
// A block's untyped parameter takes its argument's own type, as a
// function's does (§7.6, §6.4).
fn apply<F>(c: bool, a: i64[3]&, b: i64[2]&) { F(if c { a } else { b }) }
fn main() {
    var a: i64[3] = [1, 2, 3];
    var b: i64[2] = [4, 5];
    apply(a[0] > 0, a, b) { x => print(x); };
}
