// A variable bound to another plus a constant equals it only where the sum
// cannot wrap, as it does in a release build.
import std;
fn main() {
    var a: i64[3] = [1, 2, 3];
    var vals: i64[2] = [9223372036854775807, -5];
    let i = vals[0];
    let k = vals[1];
    if i > 1000 {
        let j = i + 1;
        if j < k {
            if k < a.len { print(a[k]); } // bce:keep
        }
    }
    print(99);
}
