// error: cannot call g: it may shrink ys while c is still used, and c may refer into ys
// c is a copy of k, which holds a reference rooted at r's class, and this
// call passes ys to both of g's reference parameters: h.a, read back out
// of a copy of an element of hs, may be ys (§9.5, §5.1).
struct H { a: i64[>..]& }
struct R { r: i64& }
fn g(xs: i64[>..]&, r: i64&) {
    let k = R { r: r };
    let c = k;
    xs.pop();
    xs.push(77);
    print(c.r);
}
fn main() {
    var ys: i64[>..] = [1, 2, 3];
    var zs: i64[>..] = [4];
    let hs = [H { a: ys }];
    let h = hs[0];
    g(h.a, ys[2]);
}
