// error: fields takes a variable or a field path of one
// Each field's call names the value again (§12), so it may compute nothing.
struct P { x: i64, y: i64 }
fn make() -> P { P { x: 1, y: 2 } }
fn main() {
    fields(make()) { name, f => print(name, " ", f) }
}
