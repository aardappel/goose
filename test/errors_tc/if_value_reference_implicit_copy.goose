// error: a is not fixed-size and is not copied implicitly
// A reference an if's branch gives is copied as its pointee where the if's
// destination names no type (§3.8, §4.1), `&a` as much as `a`: for a
// non-fixed pointee that is an implicit copy.
fn main() {
    var a: i64[>..] = [1, 2, 3];
    var b: i64[>..] = [4, 5];
    let c = a.len > b.len;
    let x = if c { &a } else { &b };
    a[0] = 5;
    print(x[0]);
}
