// error: cannot pop a: the elements of a[..], which the statement reads after it
// error: may be in a
fn main() {
    var a: i64[>..<] = [1];
    print((a[..])[..a.pop()]);
}
