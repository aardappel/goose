// error: cannot use y in the value assigned to x: y may refer to x
// A reference read back out of a holder is rooted inexactly: it may be the
// other parameter's array, which is settled once every call site has been
// seen (§4.4).
struct Holder { r: i64[>..<]& }
fn replace(x: i64[>..<]&, y: i64[>..<]&) { x = copy(y); }
fn main() {
    var a: i64[>..<] = [1];
    var b: i64[>..<] = [2, 3];
    let h = Holder { r: a };
    let r .= h.r;
    replace(b, r);
    print(a, b);
}
