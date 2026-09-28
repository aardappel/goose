// error: cannot pop a: r, which the assignment writes after it
// error: may be in a
fn main() {
    var a: i64[>..<] = [42];
    let r .= a[0];
    r = a.pop();
}
