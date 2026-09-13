// error: cannot pop a: an earlier expression value
// error: may still refer into a
fn main() {
    var a: i64[>..<] = [42];
    a[0] = a.pop();
}
