// error: cannot bind the payload of fixed-mode Shape by reference
enum Shape { Circle { r: i64 }, Rect { w: i64, h: i64 }, Dot }
fn f(s: Shape) -> i64 { match s { const for <N, V> in variants<Shape> { V &c => 1, } } }
fn main() { print(f(Shape.Dot)); }
