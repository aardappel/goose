// error: branches have mismatched types: i64[3] vs i64[2]
// Arrays of two types join as a slice only where a slice joins them as well,
// or the destination is one (§6.4); otherwise the value is a copy of one of
// them, which needs one type.
fn main() {
    let a: i64[3] = [1, 2, 3];
    let b: i64[2] = [4, 5];
    let c = a[0] > 0;
    let s = if c { a } else { b };
    print(s);
}
