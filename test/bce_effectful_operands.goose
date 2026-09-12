// runtime-debug: every backend must keep the bounds check after RHS effects.
import std;
fn main() {
    var a: i64[2] = [11, 22];
    var i = 2;
    fn reset() -> i64 { i = 0; return 0; }
    // The multiplication is evaluated before reset, so the index is four.
    print(a[i * 2 + reset()]); // bce:keep
}
