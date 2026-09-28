// error: branches have mismatched types: i64[3] vs i64[2]
// An array parameter is no slice that could join arrays of two types (§6.4).
fn show(x: i64[3]) { print(x); }
fn main() {
    let a: i64[3] = [1, 2, 3];
    let b: i64[2] = [4, 5];
    let c = a[0] > 0;
    show(if c { a } else { b });
}
