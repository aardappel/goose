// error: cannot use y in the value assigned to x: y may refer to x
// A call site passing one array to both parameters puts them in one root
// class, so the value built over x's contents would read them through y.
fn replace(x: i64[>..<]&, y: i64[>..<]&) { x = copy(y); }
fn main() {
    var a: i64[>..<] = [1, 2];
    replace(a, a);
    print(a);
}
