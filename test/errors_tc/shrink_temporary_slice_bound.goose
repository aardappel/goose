// error: cannot pop a: an earlier expression value
// error: may still refer into a
fn main() {
    var a: i64[>..<] = [1];
    print((a[..])[..a.pop()]);
}
