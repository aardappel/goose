// error: struct B contains itself by value
// A reaches B only through a reference, but B holds C, which holds B
// (§3.4).
struct A { v: i64, b: B? }
struct B { c: C }
struct C { a: A, b: B }
fn main() {
    let a = A { v: 1, b: null };
    print(a.v);
}
