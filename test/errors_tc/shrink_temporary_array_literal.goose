struct Holder { r: i64&, n: i64 }
fn main() {
    var a: i64[>..<] = [42];
    var n = 9;
    print([Holder { a[0], 0 }, Holder { n, a.pop() }]);
}
