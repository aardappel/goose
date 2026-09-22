// error: cannot call g: it may shrink ys while s is still used, and s may refer into ys
// The loop iterates s in place, and this call passes ys to both of g's
// parameters: h.a may be ys (§9.5), so the pop frees an element the loop
// reads next (§5.1, §6.5).
struct H { a: i64[>..]& }
fn g(xs: i64[>..]&, s: i64[:]) {
    for x in s {
        xs.pop();
        xs.push(77);
        print(x);
    }
}
fn main() {
    var ys: i64[>..] = [1, 2, 3];
    var zs: i64[>..] = [4];
    let h = H { a: ys };
    g(h.a, ys[..]);
}
