// error: variants requires a concrete ADT type, got i64
enum Shape { Circle { r: i64 }, Rect { w: i64, h: i64 }, Dot }
fn main() { const for <N, V> in variants<i64> { print(N); } }
