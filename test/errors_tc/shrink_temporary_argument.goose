// error: cannot pop a: a[0], evaluated earlier in the statement
// error: may still refer into a
fn read(r: i64&, unused: i64) { print(r); }
fn main() {
    var a: i64[>..<] = [42];
    read(a[0], a.pop());
}
