// error: cannot call f: it shrinks a while s is still used, and s may refer into a
// A nested function shrinks an array of the function it is declared in,
// which its caller gives it a view of (§5.2, §7.5).
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    fn f(s: i64[:]) {
        a.clear();
        a.push(9);
        print(s);
    }
    f(a[..]);
}
