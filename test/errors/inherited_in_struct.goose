// error: 'inherited' only applies to a function declaration's generic parameters (§7.6)
// `inherited` is contextual: a `struct` or `enum` generic list accepts only
// type parameters (§7.6).
fn f<inherited g: fn()>() { g(); }
struct S<inherited F: fn()> { v: i64 }
fn main() { var x: S<i64> = S { 1 }; print(x.v); }
