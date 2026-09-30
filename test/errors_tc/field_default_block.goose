// error: the default of field d of S leads to this construction of S, which takes it again
// A block written in the default is part of it, whatever function calls the
// block (§3.2, §7.6).
fn apply<F>() -> i64 { F() }
struct S { a: i64 = 1, d: i64 = apply() { S { .. }.a } }
fn main() {
    let x = S { .. };
    print(x.d);
}
