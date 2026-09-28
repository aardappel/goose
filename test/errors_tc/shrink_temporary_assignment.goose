// error: cannot pop a: a[0], which the assignment writes after it
// error: may be in a
fn main() {
    var a: i64[>..<] = [42];
    a[0] = a.pop();
}
