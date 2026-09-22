// error: cannot clear xs while p is still used
// error: the slice it refers to may point into it
// p refers to the slice field of h, which views xs (§5.1).
struct H { s: i64[:] }
fn main() {
    var xs: i64[>..] = [1, 2, 3];
    var h = H { s: xs[..] };
    let p: i64[:]& = h.s;
    xs.clear();
    print(p[0]);
}
