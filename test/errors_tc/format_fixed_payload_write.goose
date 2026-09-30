// error: cannot assign through this path
// A fixed-mode ADT's payload is rendered from a copy (§3.7): a format hook
// taking a part by reference is given the copy's, read-only, as a
// temporary's is (§9.5), however writable the value it was copied from.
struct A { v: i64 }
enum Shape {
    Circle { a: A, b: i64 },
    Rect { w: i64, h: i64 },
}
var g: Shape = Shape.Rect { w: 0, h: 0 };
fn format(out: u8[>..]&, a: A&) { a.v = 9; format(out, a.v); }
fn main() {
    g = Shape.Circle { a: A { v: 5 }, b: 6 };
    print(g);
}
