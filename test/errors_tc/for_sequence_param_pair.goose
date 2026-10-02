// error: cannot call g: it may shrink a while ys[..] is still used, and ys[..] may refer into a
// g's loop walks a view of what ys refers to while its body shrinks what xs
// refers to, and h.a, read back out of h, a copy of an element of hs, may
// be a (§5.2, §6.5, §9.5).
struct H { a: f64[>..<]& }
fn g(xs: f64[>..<]&, ys: f64[>..<]&) {
    for y in ys[..] {
        xs.pop();
        xs.push(0.5);
        print(y);
    }
}
fn main() {
    var a: f64[>..<] = [1.0, 2.0, 3.0];
    var b: f64[>..<] = [4.0];
    let hs = [H { a: b }];
    let h = hs[0];
    g(h.a, a);
}
