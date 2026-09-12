// runtime-debug: signed AND can preserve the sign bit.
import std;
fn main() {
    var a: i64[2] = [11, 22];
    var x: i64 = -2;
    var mask: i64 = -2;
    // Keep this a runtime operation, not a folded literal.
    let rx = &x;
    let rm = &mask;
    let i = x & mask;
    if i < a.len { print(a[i]); } // bce:keep
}
