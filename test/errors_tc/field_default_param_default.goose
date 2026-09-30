// error: the default of field d of S leads to this construction of S, which takes it again
// S's default calls f leaving out s, whose default builds an S (§3.2, §7.1).
struct S { a: i64 = 1, d: i64 = f() }
fn f(s: S = S { .. }) -> i64 { s.a }
fn main() {
    print(f(S { a: 1, d: 2 }));
    let x = S { .. };
    print(x.d);
}
