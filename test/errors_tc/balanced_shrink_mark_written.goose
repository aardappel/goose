// error: cannot call helper, which shrinks a while v
// error: is still used: it may refer into a
// §5.2, §4.4: a mark is a `let`, but setzero writes it through a reference,
// after which the resize goes back to 0 rather than to the length the call
// began with, freeing the element v views.

fn setzero(r: i64&) { r = 0; }
fn helper(s: i64[>..<]&) {
    let mark = s.len;
    s.push(7);
    setzero(mark);
    s.resize(mark);
}
fn main() {
    var a: i64[>..<] = [];
    a.push(1);
    a.push(2);
    let v = a[0..];
    helper(a);
    print(a.len, " ", v[1]);
}
