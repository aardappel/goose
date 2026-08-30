// Every call site of a `return from` function must lie within the target's
// dynamic extent (§7.9).
fn deep(n: int) -> int {
    if n == 0 { return -1 from outer; }
    n
}
fn outer(n: int) -> int { deep(n) + 1 }
fn main() {
    print(deep(1));
}
