// error: cannot use r in the value assigned to r: that value is built over the old contents of r
// A grow-shrink array assigned whole through a reference, whose new
// contents are computed from the same reference.
fn doubled(v: i64[>..<]&) -> i64[>..<] {
    var out: i64[>..<] = [];
    for x in v { out.push(x); }
    for x in v { out.push(x); }
    return out;
}
fn redo(r: i64[>..<]&) { r = doubled(r); }
fn main() {
    var h: i64[>..<] = [1, 2, 3];
    redo(h);
    print(h);
}
