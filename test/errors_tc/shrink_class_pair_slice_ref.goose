// error: cannot clear gs while pp
// error: is still used: the slice it refers to may point into gs
// pp refers to the caller's slice variable itself (§3.8, §4.1), which only
// bounds where the slice it holds points, so peek cannot tell that slice
// from the array gs names, a or b (§9.2): clearing gs may free the element
// the slice behind pp still reads (§5.2).
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
