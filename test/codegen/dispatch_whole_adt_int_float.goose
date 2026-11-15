// A call giving an ADT, and an integer where a float is declared, to an
// overload taking the whole ADT is that ordinary call. A candidate converting
// an integer to a float ranks after tag dispatch (§7.1), and dispatch took
// such an overload as every variant's case, handing it the variant's payload
// where it takes the ADT: C that no compiler accepts. A dispatch's cases take
// the variant or a reference to it (§8.2). Each call is a shape that failed:
// a payload-less variant constant, a value of the ADT, an integer variable,
// the ADT after the float, an f32, a variable-mode parameter, a call as a
// statement and an ADT with payloads.
enum E { A, B }
enum P { X { n: i64 }, Y { m: i64 } }
fn tag(k: E) -> u8[:] { match k { A => "A", B => "B" } }
fn f(k: E, v: f64) -> f64 { print(tag(k), " ", v); v }
fn g(v: f64, k: E) -> f64 { print(v, " ", tag(k)); v }
fn h(k: E, v: f32) -> f32 { print(tag(k), " ", v); v }
fn m(k: E.., v: f64) { match k { A => print("A.. ", v), B => print("B.. ", v) } }
fn p(q: P, v: f64) -> f64 {
    match q { X x => print("X ", x.n, " ", v), Y y => print("Y ", y.m, " ", v) }
    v
}
fn main() {
    let x = f(E.A, 3);
    let k: E = E.B;
    let n = 4;
    print(x + f(k, n));
    print(g(5, E.A));
    print(h(k, 6));
    m(k, 7);
    f(E.B, 8);
    let q: P = P.Y { m: 9 };
    print(p(q, 10));
}
