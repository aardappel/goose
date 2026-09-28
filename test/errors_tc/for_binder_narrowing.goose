// error: expected a value of type i32, got i64 (narrowing and sign changes require an explicit `as`)
// A typed range binder takes its bounds as values of its type (§6.5): an i64
// bound narrows to i32 only with `as`.
fn main() {
    var n: i64 = 5;
    for i: i32 in 0..n {
        print(i);
    }
}
