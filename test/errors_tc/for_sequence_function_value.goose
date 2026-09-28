// error: cannot call each, which shrinks arr: the for loop at
// error: iterates arr[..], which may refer into arr
// A function value's body runs inside the call that runs it, in the middle
// of the loop, and the call's summary carries its shrink (§5.2, §7.6).
fn each<F>(n: i64) { for i in n { F(i); } }
fn main() {
    var arr: i64[>..<] = [1, 2, 3];
    for x in arr[..] {
        each(1) { i => arr.pop(); };
        print(x);
    }
}
