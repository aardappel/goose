// error: match has no arms: its const for generated none
enum Shape { Circle { r: i64 }, Rect { w: i64, h: i64 }, Dot }
enum Never {}
fn f(n: Never) -> i64 { match n { const for <N, V> in variants<Never> { V => 1, } } }
fn main() { print(1); }
