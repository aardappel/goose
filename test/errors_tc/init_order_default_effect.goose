// error: reordering may cross a side effect, runtime check, or nonlocal exit
var state = 0;
fn effect() -> i64 { state++; state }
struct Triple { a: i64, b: i64 = effect(), c: i64 }
fn main() {
    let t = Triple { c: state, a: 0 };
    print(t);
}
