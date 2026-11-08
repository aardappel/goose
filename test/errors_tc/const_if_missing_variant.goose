// error: match does not cover variant Shape.Dot
enum Shape { Circle { r: i64 }, Dot }
fn f(s: Shape) -> i64 { match s { const for <N, V> in variants<Shape> { const if !same<V, Shape.Dot> { V v => v.r, } } } }
fn main() { print(f(Shape.Dot)); }
