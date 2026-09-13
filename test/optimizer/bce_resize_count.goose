// runtime-debug: resize reads its count before the fill value runs.
import std;
fn main() {
    var a: i64[..8] = [1];
    var n = 2;
    fn fill() -> i64 { n = 6; return 7; }
    a.resize(n, fill());
    print(a.len, " ", n);
    if n > 4 { print(a[4]); } // bce:keep
}
