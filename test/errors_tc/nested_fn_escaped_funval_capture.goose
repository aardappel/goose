// error: h names k
// error: whose scope has ended where h is called
// The same for a nested function a function value's body yields as its
// value, called once that body has ended.
fn apply<F>(n: i64) -> i64 { F(n) }
fn pass<G>(n: i64) -> i64 { apply(n, G()) }
fn main() {
    let k = 100;
    print(pass(5) { let k = 7; fn h(x: i64) -> i64 { x + k } h });
}
