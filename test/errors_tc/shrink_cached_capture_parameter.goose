fn read(r: i64&, unused: i64) { print(r); }
fn outer(xs: i64[>..<]&) {
    fn drop() -> i64 { xs.pop(); return 0; }
    drop();
    read(xs[0], drop());
}
fn main() {
    var xs: i64[>..<] = [1, 2];
    outer(xs);
}
