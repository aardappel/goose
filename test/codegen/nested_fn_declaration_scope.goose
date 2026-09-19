// A nested function names what is in scope where it is declared, whatever
// is in scope where it is called (§7.5): a loop, an if or a later
// declaration that shadows a variable or a nested function it names changes
// nothing, called directly or as a function value, and neither does a call
// from inside such a scope before one outside, which reuses the
// specialization the first one checked. Nested functions call each other in
// either order.
fn apply<F>(n: i64) -> i64 { F(n) }
fn main() {
    let k = 1;
    fn h() -> i64 { k }
    fn hk(n: i64) -> i64 { n + k }
    for i in 0..2 { let k = 10 + i; print(h(), " ", apply(5, hk), " ", k); }
    if k > 0 { let k = 2; print(h(), " ", apply(5, hk), " ", k); }
    print(h());
    let k = 3;
    print(h(), " ", k);
    // The nested function a nested function calls.
    fn g() -> i64 { 1 }
    fn f() -> i64 { g() * 10 }
    if true { fn g() -> i64 { 2 } print(f(), " ", g()); }
    if true { let g = 5; print(f(), " ", g); }
    fn g() -> i64 { 3 }
    print(f(), " ", g());
    // Two levels deep, shadowed at every level.
    let a = 1;
    fn outer() -> i64 {
        let b = 20;
        fn inner() -> i64 { a * 100 + b + later() }
        let a = 300;
        if a > 0 { let b = 4000; print(inner(), " ", apply(1) { inner() + b + it }); }
        inner() + a
    }
    fn later() -> i64 { a * 1000 }
    for i in 0..2 { let a = 7; let b = 8; print(outer(), " ", a + b + i); }
    // Calls to a nested function declared further on, named and UFCS.
    let step = 1;
    recursive fn even(n: i64) -> bool { if n == 0 { return true; } return odd(n - step); }
    recursive fn odd(n: i64) -> bool { if n == 0 { return false; } return even(n - step); }
    fn e() -> i64 { 3.thrice() }
    fn thrice(n: i64) -> i64 { n * 3 }
    if true { let step = 2; print(even(10), " ", odd(7), " ", e(), " ", step); }
}
