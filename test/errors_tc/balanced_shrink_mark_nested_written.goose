// error: mark is bound to a writable reference here, through which it may change, while the resize at
// error: balanced_shrink_mark_nested_written.goose:13 relies on it still holding the length it was bound to
// §5.2, §4.4, §7.5: spoil is checked where it is first called, after the
// resize, and writes the mark it names through a reference; the loop's next
// resize then goes back to 0 while v still views a.

fn setzero(r: i64&) { r = 0; }
fn helper(s: i64[>..<]&) {
    let mark = s.len;
    fn spoil() { setzero(mark); }
    for i in 2 {
        s.push(7);
        s.resize(mark);
        spoil();
    }
}
fn main() {
    var a: i64[>..<] = [1, 2];
    let v = a[0..];
    helper(a);
    print(a.len, " ", v[1]);
}
