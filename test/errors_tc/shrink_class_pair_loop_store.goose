// error: cannot call g: it may shrink ys while k is still used, and k may refer into ys
// The reference into ys that the body stores into k after the pop meets the
// pop on the next iteration, and this call passes ys to both of g's
// parameters (§9.5, §5.1).
struct H { a: i64[>..]& }
struct R { r: i64& }
fn g(xs: i64[>..]&, ys: i64[>..]&) {
    var own: i64[1] = [0];
    var k = R { r: own[0] };
    for i in 2 {
        xs.pop();
        xs.push(70 + i);
        print(k.r);
        k = R { r: ys[2] };
    }
}
fn main() {
    var ys: i64[>..] = [1, 2, 3];
    var zs: i64[>..] = [4];
    let h = H { a: ys };
    g(h.a, ys);
}
