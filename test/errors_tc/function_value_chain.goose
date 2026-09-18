// error: expected a value of type u8, got i64
// The two calls in main bind different named functions, and the two in pick
// blocks that differ only in their bodies: each pair is two instantiations
// (§7.7) on one line that the argument types alone would print alike. The
// chain names the function values, a block by the position of its `{`: this
// error is in `store<F = {block at function_value_chain.goose:11:72}>(i64)`,
// the second block in pick, reached from `pick<G = wide>(i64)`.
fn small() -> u8 { 1 }
fn wide() -> i64 { 300 }
fn store<F>(n: i64) -> i64 { var r: u8 = 0; r = F(); n }
fn pick<G>(n: i64) -> i64 { let a = store(n) { small() }; a + store(n) { G() } }
fn main() {
    print(pick(3, small), " ", pick(3, wide));
}
