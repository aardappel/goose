// error: .= rebinds references; the target has type i64
// A block written in a function without type parameters is no generic code
// (§3.8), whatever generic function calls it.
fn each<T, F>(xs: T[:]) { for x in xs { F(x); } }
fn main() {
    var last = 0;
    let xs = [1, 2, 3];
    each(xs) { last .= it; };
    print(last);
}
