// error: no matching overload for call to f
// A UFCS call resolves in the namespace of its receiver's type before the
// call's own, and sets never merge: t.f(2) does not reach the global f
// that a::f's set has no match for.
struct a::T { v: i64 }
fn a::f(t: T) -> i64 { t.v }
fn f(t: a::T, k: i64) -> i64 { t.v * k }
fn main() {
    let t = a::T { 1 };
    print(t.f(2));
}
