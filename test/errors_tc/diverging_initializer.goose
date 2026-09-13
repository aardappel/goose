// error: initializer has no value
// A declaration cannot take its type from an initializer that never
// produces one, like a call to abort.
fn f(x: i64) -> i64 {
    let v = if x == 0 { return 1; } else { return 2; };
    v
}
fn main() {
    print(f(0));
}
