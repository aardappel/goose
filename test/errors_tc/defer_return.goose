// error: return cannot leave a defer body (§6.6)
// A defer body runs at the exits of its block, so it cannot exit anything
// itself.
fn f() -> i64 {
    defer { return 1; }
    return 2;
}
fn main() { print(f()); }
