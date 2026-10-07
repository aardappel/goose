// error: reordering may cross a side effect, runtime check, or nonlocal exit
struct Pair { a: i64, b: i64 }
fn make(x: i64) -> Pair {
    let p = Pair { b: { if x == 0 { return Pair { 0, 0 }; } x }, a: 0 };
    p
}
fn main() { print(make(1)); }
