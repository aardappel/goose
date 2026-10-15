// error: expected a value of type i64, got bool
// The block is checked at each field's type (§12, §7.7): the bool field fails.
struct P { x: i64, on: bool }
fn main() {
    let p = P { x: 1, on: true };
    var sum = 0;
    fields(p) { name, f => let v: i64 = f; sum += v; }
    print(sum);
}
