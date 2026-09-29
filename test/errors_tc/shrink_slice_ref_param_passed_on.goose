// error: cannot clear a while x
// error: is still used: it may refer into a
// inner's parameter is given outer's, which names s: the slice inner loads
// through it is s's, which views a, however many calls pass the reference
// on (§5.2).
fn inner(q: i64[:]&) -> i64[:] { return q; }
fn outer(p: i64[:]&) -> i64[:] { return inner(p); }
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    var s = a[..];
    let r = &s;
    let x = outer(r);
    a.clear();
    a.push(9);
    print(x[0]);
}
