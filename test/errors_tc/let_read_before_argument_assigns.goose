// error: variable x may be used before it is assigned
// The arguments are evaluated in order: a block run for the first reads x,
// which only a block run for the second assigns (§4.4).
fn add(a: i64, b: i64) -> i64 { a + b }
fn once<F>() -> i64 { F(); return 1; }
fn with<F>(v: i64) -> i64 { return F(v); }
fn main() {
    let x: i64;
    print(add(with(3) { it + x }, once() { x = 1; }));
    print(x);
}
