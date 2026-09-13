// error: cannot pop a: an earlier expression value
// error: may still refer into a
fn values(a: i64[>..<]&) -> i64[:], i64 {
    return a[..], a.pop();
}
fn main() {
    var a: i64[>..<] = [42];
    let x, n = values(a);
    print(x, n);
}
