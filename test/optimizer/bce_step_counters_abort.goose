// A counter stepped twice per iteration outruns the loop index, so its
// index check stays and fires once the counter passes the end.
import std;
fn main() {
    var a: i64[4] = [1, 2, 3, 4];
    var w = 0;
    var s = 0;
    for i in 3 {
        w += if i >= 0 { 1 } else { 0 };
        w += 1;
        s += a[w];                            // bce:keep
        print(s);
    }
}
