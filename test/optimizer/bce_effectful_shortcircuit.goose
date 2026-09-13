// runtime-debug: facts from a mutating operand cannot narrow the next operand.
import std;
fn main() {
    var a: i64[2] = [11, 22];
    var i = 0;
    fn read() -> i64 { i * 2 }
    fn mutate() -> i64 { i = 2; return 2; }
    if read() < mutate() && a[i] == 11 { print(1); } // bce:keep
}
