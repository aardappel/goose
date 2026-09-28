// error: cannot call apply: it shrinks a while r is still used, and r may refer into a
// A function value's body runs in the middle of the function calling it:
// apply's parameter, a reference into a, is used after the block pops a and
// pushes over the freed slot (§5.2, §7.6).
fn apply<F>(r: i64&) -> i64 {
    let n = F();
    return r + n;
}
fn main() {
    var a: i64[>..<] = [1, 2];
    print(apply(a[1]) { a.pop(); a.push(99); 0 });
}
