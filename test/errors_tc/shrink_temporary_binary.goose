// error: cannot pop a: an earlier expression value
// error: may still refer into a
fn main() {
    var a: i64[>..<] = [42];
    print(a[..] == (block { a.pop(); break [42]; }));
}
