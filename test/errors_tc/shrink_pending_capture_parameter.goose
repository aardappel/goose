fn read(r: i64&, unused: i64) -> i64 { return r; }
// Both captures share a root; seeing its scalar alias must not hide the array.
fn outer(first: i64&, xs: i64[>..<]&) {
    recursive fn step(n: i64) -> i64 {
        if n > 0 { return read(xs[0], step(n - 1)); }
        xs.pop();
        return 0;
    }
    print(step(1));
}
fn main() {
    var xs: i64[>..<] = [1, 2];
    outer(xs[0], xs);
}
