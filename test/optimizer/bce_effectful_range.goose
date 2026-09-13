// runtime-debug: the lower endpoint is saved before upper-endpoint effects.
import std;
fn main() {
    var a: i64[2] = [11, 22];
    var i = -1;
    fn upper() -> i64 { i = 0; return 2; }
    for j in i..upper() { print(a[j]); } // bce:keep
}
