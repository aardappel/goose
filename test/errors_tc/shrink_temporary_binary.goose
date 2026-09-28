// error: cannot pop a: a[..], evaluated earlier in the statement
// error: may still refer into a
fn main() {
    var a: i64[>..<] = [42];
    print(a[..] == (block { a.pop(); break [42]; }));
}
