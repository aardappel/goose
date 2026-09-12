fn exercise<F>() {
    var a: i64[>..<] = [42];
    F(a[0], a.pop());
}
fn main() { exercise() { r: i64&, unused: i64 => print(r) } }
