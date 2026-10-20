// error: const for variant Circle belongs to Small, not Shape
enum Shape { Circle { r: i64 }, Rect { w: i64, h: i64 }, Dot }
enum Small { Circle, Rect }
fn f(s: Shape) -> i64 { match s { const for <N, V> in variants<Small> { V => 1, } } }
fn main() { let s: Shape = Shape.Dot; print(f(s)); }
