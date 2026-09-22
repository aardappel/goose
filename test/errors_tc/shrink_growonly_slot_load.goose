// error: cannot clear xs while v is still used
// error: it may hold a reference or slice into it
// The slice loaded through `&views[0]` is read out of views, whose stores
// only bound where it points, not views itself (§3.8, §5.1, §9.5).
fn main() {
    var xs: i64[>..] = [1, 2];
    var views: (i64[:])[>..] = [];
    views.push(xs[..]);
    let p = &views[0];
    let v = p;
    xs.clear();
    print(v[0]);
}
