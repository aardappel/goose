// error: cannot assign to let x
// Two arguments of one call each run a block that assigns x: the second
// finds it assigned by the first, whose effects it is checked after (§4.4).
fn add(a: i64, b: i64) -> i64 { a + b }
fn once<F>() -> i64 { F(); return 1; }
fn main() {
    let x: i64;
    print(add(once() { x = 1; }, once() { x = 2; }));
    print(x);
}
