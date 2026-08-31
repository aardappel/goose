// Recursion is opt-in via `recursive fn` (§7.8).
fn fact(n: i64) -> i64 {
    if n <= 1 { 1 } else { n * fact(n - 1) }
}
fn main() {
    print(fact(5));
}
