// error: unknown identifier: limit
// A default names nothing of the code calling the function (§7.1): the
// caller's `limit` is not in scope where the default is written.
fn clamp(x: i64, hi: i64 = limit) -> i64 { if x > hi { hi } else { x } }
fn main() {
    let limit = 10;
    print(clamp(20));
}
