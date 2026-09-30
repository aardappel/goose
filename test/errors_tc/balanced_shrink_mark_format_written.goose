// error: cannot call helper, which shrinks a while v
// error: is still used: it may refer into a
// §5.2, §3.7, §4.4: a mark is a `let`, but print hands it to the format
// overload taking an i64 by reference, which makes it 0, after which the
// resize goes back to 0 rather than to the length the call began with,
// freeing the element v views.

fn format(out: u8[>..]&, v: i64&) { v = 0; out.append("m"); }
fn helper(s: i64[>..<]&) {
    let mark = s.len;
    s.push(7);
    print(mark);
    s.resize(mark);
}
fn main() {
    var a: i64[>..<] = [];
    a.push(1);
    a.push(2);
    let v = a[0..];
    helper(a);
    print(a.len as u64, " ", v[1] as u64);
}
