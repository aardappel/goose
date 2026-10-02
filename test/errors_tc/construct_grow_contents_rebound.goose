// error: cannot call mk, which grows y: the run appended to x is still under construction
// h.r may be a or b after its rebinding, so f may grow the array it is
// appending to (§1.3, §9.5).
struct Holder { r: i64[>..]& }
fn mk(y: i64[>..]&) -> i64[>..] { for i in 100 { y.push(9); } return [1, 2]; }
fn f(x: i64[>..]&, y: i64[>..]&) { x.append(mk(y)); }
fn main() {
    var a: i64[>..] = [1];
    var b: i64[>..] = [];
    var h = Holder { r: a };
    if a.len > 0 { h.r .= b; }
    f(b, h.r);
    print(a.len, b.len);
}
