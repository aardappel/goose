// error: returning a reference into a temporary
// A function value's call gives a copy of its body's value, a temporary of
// the calling statement as any call's result is (§9.2).
var g: i64[3] = [1, 2, 3];
fn view<F>() -> const i64[:] { F()[..] }
fn main() {
    let v = view() { g };
    print(v[0]);
}
