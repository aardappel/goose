// error: returning a reference rooted in local, which dies with this function
enum Shape { Circle { r: i64 }, Rect { w: i64, h: i64 }, Dot }
enum E { A { x: i64 }, B { y: i64 } }
fn one(c: E.A&) -> i64& { c.x }
fn one(c: E.B&) -> i64& { c.y }
fn leak<T>(proto: T&) -> i64& { var local: T = copy(proto); match local { const for <N, V> in variants<T> { V &c => one(c), } } }
fn main() { let p: E.. = E.A { x: 1 }; print(leak(p)); }
