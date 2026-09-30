// error: the default of field d of S leads to this construction of S, which takes it again
// A default constructing its own type with itself would run without end, and
// every construction checks the defaults it takes (§3.2).
struct S { a: i64 = 1, d: i64 = S { .. }.a }
fn main() {
    let x = S { .. };
    print(x.d);
}
