fn read(r: i64&, unused: i64) { print(r); }
fn main() {
    var a: i64[>..<] = [42];
    read(a[0], a.pop());
}
