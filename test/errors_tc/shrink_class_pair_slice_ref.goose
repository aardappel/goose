// error: cannot call peek: it may shrink a while pp is still used, and pp may refer into a
// pp refers to s by reference, so it is rooted where s's slice points, a,
// and g may be a as well (§9.2): clearing gs frees the element the slice
// behind pp still reads (§5.2).
fn peek(pp: i64[:]&, gs: i64[>..<]&) {
    gs.clear();
    gs.push(5);
    print(pp[0]);
}
fn main() {
    var a: i64[>..<] = [];
    a.push(77);
    var b: i64[>..<] = [];
    let c = a.len > 0;
    var s = a[..];
    let g: i64[>..<]& = if c { a } else { b };
    peek(s, g);
}
