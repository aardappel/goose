// error: storing a reference rooted at a, which does not outlive the destination
// A fixed-mode ADT's payload is rendered from a copy (§3.7), since nothing may
// refer into it while an overload may replace its variant (§3.5): a format
// hook taking a part by reference is given the copy's, a temporary, which it
// cannot keep past its call.
struct A { v: i64 }
enum Shape {
    Circle { a: A, b: i64 },
    Rect { w: i64, h: i64 },
}
var g: Shape = Shape.Rect { w: 0, h: 0 };
var kept: const A? = null;
fn format(out: u8[>..]&, a: const A&) { kept .= a; format(out, a.v); }
fn main() {
    g = Shape.Circle { a: A { v: 5 }, b: 6 };
    print(g);
    g = Shape.Rect { w: 4096, h: 77 };
    if kept { print(kept.v); }
}
