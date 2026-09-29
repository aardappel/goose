// error: cannot call peek: it may shrink a while pp is still used, and pp may refer into a
// pp's slot holds s's slice, a view of a, which peek cannot tell from the
// array gs names: this call passes a or b there (§9.2), so clearing gs may
// free the element the slice behind pp still reads (§5.2).
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
