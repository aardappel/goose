// error: cannot pop arr while h is still used: a reference into it was stored there
// A holder read out of a call's result holds what the call put in it, which
// here points into arr.
struct H { r: i64& }
fn wrap(a: i64[>..]&) -> H[] { [H { r: a[0] }] }
fn main() {
    var arr: i64[>..] = [1, 2, 3];
    let h = wrap(arr)[0];
    arr.pop(); arr.pop(); arr.pop();
    arr.push(42);
    print(h.r);
}
