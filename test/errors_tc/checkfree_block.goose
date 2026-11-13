// error: checkfree fn each keeps a bounds check in code written in fn main: nothing shows x >= 0
// A block runs inside the function calling it, so it is judged as part of it. (docs/design/index_types.md)
checkfree fn each<F>(xs: i32[:]) { for x in xs { F(x); } }
fn main() {
    var a: i32[>..] = [];
    a.push(0);
    var t = 0;
    each(a) { x => t += a[x]; };
    print(t);
}
