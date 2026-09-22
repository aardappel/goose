// error: cannot clear xs while hp is still used
// error: what it refers to may hold a reference or slice into it
// h holds a slice of xs; a reference to h keeps it live after h's own last
// use (§5.1).
struct H { s: i64[:] }
fn main() {
    var xs: i64[>..] = [1, 2, 3];
    var h = H { s: xs[..] };
    let hp: H& = h;
    xs.clear();
    print(hp.s[0]);
}
