// error: duplicate parameter name: a
// A function value names each of its parameters once, as a function does
// (§7.6).
fn both<F>(a: i64, b: i64) -> i64 { F(a, b) }
fn main() {
    print(both(1, 2) { a, a => a });
}
