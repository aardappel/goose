// error: cannot call mk, which grows y: the run appended to x is still under construction
// A reference read back out of a holder is rooted inexactly: it may point
// into the other parameter's array, so the two classes are settled as
// possibly one array once every call site has been seen (§1.3(4)).
struct Holder { r: i64[>..]& }
fn mk(y: i64[>..]&) -> i64[>..] { y.push(9); return [1, 2]; }
fn f(x: i64[>..]&, y: i64[>..]&) { x.append(mk(y)); }
fn main() {
    var a: i64[>..] = [1];
    var b: i64[>..] = [];
    let h = Holder { r: a };
    let r .= h.r;
    f(b, r);
    print(a, b);
}
