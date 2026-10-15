// error: fields takes a struct value and a function value
// fields has no use without a function value to call (§12).
struct P { x: i64, y: i64 }
fn main() {
    let p = P { x: 1, y: 2 };
    fields(p);
}
