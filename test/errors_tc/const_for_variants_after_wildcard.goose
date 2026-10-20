// error: _ must be the last match arm
enum Shape { Circle { r: i64 }, Rect { w: i64, h: i64 }, Dot }
fn f(s: Shape) -> i64 { match s { _ => 0, const for <N, V> in variants<Shape> { V => 1, } } }
fn main() { let s: Shape = Shape.Dot; print(f(s)); }
