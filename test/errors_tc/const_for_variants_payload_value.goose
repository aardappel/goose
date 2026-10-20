// error: which has a payload: construct it with V { ... }
enum Shape { Circle { r: i64 }, Rect { w: i64, h: i64 }, Dot }
fn f<E>() -> i64 { var n = 0; const for <N, V> in variants<E> { let x: E = V; n++; } n }
fn main() { print(f<Shape>()); }
