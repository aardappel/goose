// error: return cannot leave a defer body (§6.6)
fn outer() -> i64 {
    return inner() + 1;
}
fn inner() -> i64 {
    defer { return 5 from outer; }
    return 2;
}
fn main() { print(outer()); }
