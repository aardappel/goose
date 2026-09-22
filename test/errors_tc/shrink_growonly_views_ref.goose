// error: cannot clear xs while r is still used
// error: what it refers to may hold a reference or slice into it
// A reference to a whole resizable array is the path to it, but the slices
// that array holds point into xs (§5.1).
fn main() {
    var xs: i64[>..] = [1];
    var views: (i64[:])[>..] = [];
    views.push(xs[..]);
    let r: (i64[:])[>..]& = views;
    xs.clear();
    print(r[0][0]);
}
