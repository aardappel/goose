// error: cannot clear a while t is still used
// A holder returned out of a block through two calls: each maps the slice
// it holds onto its own call's argument, so first_tail's result may hold a
// view of a, which the clear frees.
struct Tail { s: i64[:] }
var none: i64[1] = [0];
fn each_tail<F>(xs: i64[:]) { for i in xs.len { F(xs[i..]); } }
fn each_via<F>(xs: i64[:]) { each_tail(xs) { s => F(s); }; }
fn first_tail(xs: i64[:], limit: i64) -> Tail {
    each_via(xs) { s => if s[0] > limit { return Tail { s: s }; } };
    return Tail { s: none[..] };
}
fn main() {
    var a: i64[>..] = [5, 6, 7, 8];
    let t = first_tail(a, 6);
    a.clear();
    print(t.s.len);
}
