// error: reordering may cross a side effect, runtime check, or nonlocal exit
struct Triple { a: i64, b: i64, c: i64 }
var state = 0;
fn effect() -> i64 { state++; state }
fn main() {
    // b stays in the middle, but c's read crosses it.
    let t = Triple { c: state, b: effect(), a: 0 };
    print(t);
}
