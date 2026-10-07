// error: reordering may cross a side effect, runtime check, or nonlocal exit
struct Pair { a: i64, b: i64 }
fn get(xs: const i64[:], index: i64) -> Pair { Pair { b: xs[index], a: 0 } }
fn main() { print(get([1], 0)); }
