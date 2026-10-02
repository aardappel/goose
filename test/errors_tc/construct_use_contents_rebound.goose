// error: cannot use y in the value assigned to x: y may refer to x
// h.r may be a or b after its rebinding, so replace's two parameters may
// be one array (§4.4, §9.5).
struct Holder { r: i64[>..<]& }
fn replace(x: i64[>..<]&, y: i64[>..<]&) { x = copy(y); }
fn main() {
    var a: i64[>..<] = [1];
    var b: i64[>..<] = [2, 3];
    var h = Holder { r: a };
    if b.len > 0 { h.r .= b; }
    replace(b, h.r);
    print(a, b);
}
