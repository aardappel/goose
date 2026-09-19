// runtime-debug: a cast wraps at its own type (§6.3), also when its value is
// stored into a wider slot, so it carries its operand's bounds only where the
// value provably fits: `k as! i8` of a k in [0, 300) is not k.
import std;
fn at(a: i64[:], k: i64) -> i64 {
    if k >= 0 && k < 100 && k < a.len {
        let j: i64 = k as i8;
        print(a[j]); // bce:elide
    }
    if k >= 0 && k < a.len {
        let j: i64 = k as! i8;
        return a[j]; // bce:keep
    }
    -1
}
fn main() {
    var a: i64[>..] = [7; 300];
    print(at(a, 5));
    print(at(a, 200));
}
