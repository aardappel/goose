// varint fields are written only at construction (§3.6).
struct S { v: varint }
fn main() {
    var s = S { v: 1 };
    s.v = 2;
}
