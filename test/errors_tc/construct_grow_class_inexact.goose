// error: cannot call mk, which grows y: the run appended to x is still under construction
// A reference read back out of a copy of an element of hs is rooted
// inexactly: it may point into the other parameter's array, so the two
// classes are settled as possibly one array once every call site has been
// seen (§1.3(4)).
struct Holder { r: i64[>..]& }
fn mk(y: i64[>..]&) -> i64[>..] { y.push(9); return [1, 2]; }
fn f(x: i64[>..]&, y: i64[>..]&) { x.append(mk(y)); }
fn main() {
    var a: i64[>..] = [1];
    var b: i64[>..] = [];
    let hs = [Holder { r: a }, Holder { r: b }];
    let h = hs[0];
    let r .= h.r;
    f(b, r);
    print(a, b);
}
