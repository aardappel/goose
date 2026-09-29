// error: mark is bound to a writable reference here, through which it may change, while the resize at
// error: balanced_shrink_mark_written_after.goose:12 relies on it still holding the length it was bound to
// §5.2, §4.4: the resize comes first, but a writable reference to the mark
// anywhere is enough: here the loop's next resize runs after setzero, going
// back to 0 while v still views a.

fn setzero(r: i64&) { r = 0; }
fn helper(s: i64[>..<]&) {
    let mark = s.len;
    for i in 2 {
        s.push(7);
        s.resize(mark);
        setzero(mark);
    }
}
fn main() {
    var a: i64[>..<] = [1, 2];
    let v = a[0..];
    helper(a);
    print(a.len, " ", v[1]);
}
