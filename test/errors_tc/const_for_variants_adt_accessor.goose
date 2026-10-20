// error: const for accessor belongs to Shape.Circle, not Shape
enum Shape { Circle { r: i64 }, Rect { w: i64, h: i64 }, Dot }
fn main() { let s: Shape = Shape.Dot; const for <F, A> in fields<Shape.Circle> { print(s.A); } }
