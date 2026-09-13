// error: cannot call grow_and_copy, which grows a: the value assigned to a is still under construction, and the growth would land inside it (§1.3)
// The new contents of a whole assignment are built at the array's base
// while the right-hand side runs (§4.4), and the callee pushes into it.
fn grow_and_copy(a: i64[>..]&) -> i64[>..] {
    a.push(100);
    var r: i64[>..] = [];
    for x in a { r.push(x); }
    return r;
}
fn main() {
    var a: i64[>..] = [1, 2, 3];
    a = grow_and_copy(a);
    print(a);
}
