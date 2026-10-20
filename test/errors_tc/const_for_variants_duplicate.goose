// error: variant Dot is already matched by an earlier arm
enum Shape { Circle { r: i64 }, Rect { w: i64, h: i64 }, Dot }
fn f(s: Shape) -> i64 { match s { Dot => 0, const for <N, V> in variants<Shape> { V => 1, } } }
fn main() { let s: Shape = Shape.Dot; print(f(s)); }
