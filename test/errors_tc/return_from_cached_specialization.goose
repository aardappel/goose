// step reuses mid inside g<i32> before walk's `return ... from g` gives mid
// its g<i64> target, so the innermost g on that call path is the wrong one.
fn h() -> i64 {
    let v: i64 = 1;
    print("outer g returned ", g(v, 2));
    return 0;
}
fn maybe_h(n: i64) { if n > 100 { return -1 from h; } }
fn g<T>(x: T, n: i64) -> T { step(x, n); return x; }
fn step(x: i64, n: i64) { walk(n); }
fn step(x: i32, n: i64) { maybe_h(n); mid(n); }
recursive fn walk(n: i64) {
    if n == 2 { mid(n); return; }
    if n == 1 {
        let z: i32 = 7;
        print("inner g returned ", g(z, 0));
        return;
    }
    return 42 from g;
}
recursive fn mid(n: i64) { walk(n - 1); }
fn main() { print(h()); }
