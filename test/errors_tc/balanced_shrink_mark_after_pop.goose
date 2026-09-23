// error: cannot call helper, which shrinks s while v
// error: is still used: it may refer into s
// A mark taken after an unbalanced shrink may be shorter than the array was
// when the call began (§5.2): the pop already freed what v views.
fn helper(s: i64[>..<]&) {
    s.pop();
    let mark = s.len;
    s.push(4);
    s.resize(mark);
}
fn main() {
    var s: i64[>..<] = [9];
    let v = s[..];
    helper(s);
    print(v[0]);
}
