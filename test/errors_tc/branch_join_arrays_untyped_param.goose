// error: branches have mismatched types: i64[3] vs i64[2]
// An untyped parameter takes its argument's own type (§7.1): no destination
// type that could join arrays of two types as a slice (§6.4).
fn show(x) { print(x); }
fn main() {
    let a: i64[3] = [1, 2, 3];
    let b: i64[2] = [4, 5];
    let c = a[0] > 0;
    show(if c { a } else { b });
}
