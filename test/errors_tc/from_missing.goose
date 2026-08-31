// Every call site of a `return from` function must lie within the target's
// dynamic extent (§7.9).
fn deep(n: i64) -> i64 {
    if n == 0 { return -1 from outer; }
    n
}
fn outer(n: i64) -> i64 { deep(n) + 1 }
fn main() {
    print(deep(1));
}
