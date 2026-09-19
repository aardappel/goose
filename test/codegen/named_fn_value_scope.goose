// A nested function passed by name as a function value sees the variables of
// the scope it is declared in, wherever it is named: not those of a nested
// function it is named in, which may declare the same names, of the same type
// or another.
fn apply<F>(n: i64) -> i64 { F(n) }
fn twice<F>(n: i64) -> i64 { F(F(n)) }
var y = 2;
fn main() {
    let x = 1;
    fn g(n: i64) -> i64 { n + x * 10 + y }
    fn h() -> i64 { let x = 100; let y = 300; apply(5, g) + x + y }
    fn k() -> u8[] { let x = "text"; let y = "more"; if twice(5, g) == 29 { x } else { y } }
    print(h(), " ", k());
    // Two nested functions deep, and where g is declared.
    fn outer() -> i64 {
        let x = 1000;
        fn inner() -> i64 { let x = 5000; twice(1, g) + x }
        inner() + x
    }
    print(outer(), " ", apply(5, g));
}
