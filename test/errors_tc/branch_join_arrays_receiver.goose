// error: branches have mismatched types: i64[3] vs i64[2]
// A member builtin taking any receiver takes it as its UFCS call checked it
// (§7.1), with no destination type to join arrays of two types as a slice
// (§6.4).
fn main() {
    let a: i64[3] = [1, 2, 3];
    let b: i64[2] = [4, 5];
    let c = a[0] > 0;
    print((if c { a } else { b }).bytes_of().len);
}
