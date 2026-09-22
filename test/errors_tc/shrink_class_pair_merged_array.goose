// error: cannot call peek: it may shrink a while p is still used, and p may refer into a
// The same with the array itself: g may be a, whose slice p is, so clearing
// gs frees the element p still reads (§5.2, §9.2).
fn peek(p: i64[:], gs: i64[>..<]&) {
    gs.clear();
    print(p[0]);
}
fn main() {
    var a: i64[>..<] = [];
    a.push(77);
    var b: i64[>..<] = [];
    let c = a.len > 0;
    let g: i64[>..<]& = if c { a } else { b };
    peek(a[..], g);
}
