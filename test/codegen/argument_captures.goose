// Variables bound inside a call's argument and captured by a nested function
// or a function value. The argument is checked once for overload resolution
// and again against the chosen parameter type; the function the first check
// specialized is reused by the second without being checked again, and still
// captures the variables codegen declares. An argument's argument is checked
// four times.
enum M { A { n: i64 }, B }
enum Shape { Circle { r: i64 }, Square { s: i64 } }
fn f(n: i64, m: i64) -> i64 { n + m }
fn apply<F>(n: i64) -> i64 { F(n) }
fn area(c: Shape.Circle, k: i64) -> i64 { c.r * 3 + k }
fn area(q: Shape.Square, k: i64) -> i64 { q.s * q.s + k }
fn main() {
    let m: M = M.A { n: 5 };
    var mv: M.. = M.A { n: 6 };
    let xs: i64[3] = [1, 2, 3];
    var ys: i64[3] = [4, 5, 6];
    var ss: u8[][>..] = [];
    ss.push("ab");
    ss.push("cde");
    // A match arm's binder, by value and by reference.
    print(f(3, match m { A a => { fn g() -> i64 { a.n * 2 } print(a.n); g() }, B => 0 }));
    print(f(3, match mv { A &a => { fn g() -> i64 { a.n * 2 } g() }, B => 0 }));
    // A for loop's variable: an element by value and by reference, a range, an
    // index, a non-fixed element.
    print(f(3, block { var t = 0; for x in xs { fn h() -> i64 { x } t += h(); } break t; }));
    print(f(3, block { var t = 0; for &y in ys { fn h() -> i64 { y * 3 } t += h(); } break t; }));
    print(f(3, block { var t = 0; for i in 1..5 { fn h() -> i64 { i } t += h(); } break t; }));
    print(f(3, block { var t = 0; for x, i in xs { fn h() -> i64 { i * 10 + x } t += h(); } break t; }));
    print(f(3, block { var t = 0; for s in ss { fn h() -> i64 { s.len } t += h(); } break t; }));
    // A let.
    print(f(3, block { let k = 7; fn g() -> i64 { k * 2 } print(k); break g(); }));
    // Captured by a function value.
    print(f(3, match m { A a => apply(4) { a.n + it }, B => 0 }));
    print(f(3, block { var t = 0; for x in xs { t += apply(4) { x + it }; } break t; }));
    // A receiver, a dispatched call's argument, an argument's argument.
    print((match m { A a => { fn g() -> i64 { a.n + 1 } g() }, B => 0 }).f(3));
    let sh: Shape = Shape.Square { s: 4 };
    print(area(sh, match m { A a => { fn g() -> i64 { a.n } g() }, B => 0 }));
    print(f(1, f(2, match m { A a => { fn g() -> i64 { a.n * 2 } g() }, B => 0 })));
}
