struct Holder { r: i64&, n: i64 }
fn main() {
    var a: i64[>..<] = [42];
    print(Holder { a[0], a.pop() });
}
