// error: null is only a value of optional types, not i64
// null is a value of optional types only (§3.8).
fn main() {
    let x: i64 = null;
    print(x);
}
