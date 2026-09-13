// runtime-debug: the lower endpoint is saved before the upper one shifts it.
import std;
fn main() {
    var a: i64[2] = [11, 22];
    var i = -1;
    for j in i..({ i += 1; 2 }) { print(a[j]); } // bce:keep
}
