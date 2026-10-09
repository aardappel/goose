// A `continue` inside a `block { }` still goes to the loop around it, so it
// skips the push after the block: the loop does not push once per
// iteration, and its count states no length.
import std;
fn main() {
    var skip: bool[>..] = [false, true, true];
    var a: i64[>..] = [];
    for i in 3 {
        block {
            if skip[i] { continue; }
        }
        a.push(i);
    }
    print(a.len);
    print(a[2]); // bce:keep
}
