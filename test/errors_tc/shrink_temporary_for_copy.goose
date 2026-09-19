// error: cannot pop arr while h is still used: a reference into it was stored there
// A for binding copies each holder out of a call's result, and with it what
// the call put in it.
struct H { r: i64& }
fn wrap(a: i64[>..]&) -> H[] { [H { r: a[0] }] }
fn main() {
    var arr: i64[>..] = [1, 2, 3];
    for h in wrap(arr) {
        arr.pop(); arr.pop(); arr.pop();
        arr.push(42);
        print(h.r);
    }
}
