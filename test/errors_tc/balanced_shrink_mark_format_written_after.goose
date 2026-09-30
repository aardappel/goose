// error: mark is bound to a writable reference here, through which it may change, while the resize at
// error: balanced_shrink_mark_format_written_after.goose:13 relies on it still holding the length it was bound to
// §5.2, §3.7, §4.4: the resize comes first, but format hands the mark to the
// overload taking an i64 by reference: the loop's next resize runs after it
// has made the mark 0, going back to 0 while v still views a.

fn format(out: u8[>..]&, v: i64&) { v = 0; out.append("m"); }
fn helper(s: i64[>..<]&) {
    let mark = s.len;
    var b: u8[>..] = "";
    for i in 2 {
        s.push(7);
        s.resize(mark);
        b.format(mark);
    }
    print(b);
}
fn main() {
    var a: i64[>..<] = [1, 2];
    let v = a[0..];
    helper(a);
    print(a.len as u64, " ", v[1] as u64);
}
