// error: cannot use s.own in the value assigned to s.r
// Assigning through a reference field writes the array it refers to, which
// may be another field of the same struct: the right-hand side reading
// that field is `x = f(x)` (§4.4).
fn twice(xs: const u8[:]) -> u8[>..<] {
    var r: u8[>..<] = [];
    r.append(xs);
    r.append(xs);
    return r;
}
struct D { n: i64, r: u8[>..<]&, own: u8[>..<] }
fn run(s: D&) { s.r = twice(s.own); }
fn main() {
    var x: u8[>..<] = [];
    var d = D { n: 0, r: x, own: [1, 2, 3] };
    d.r .= d.own;
    run(d);
    print(d.own);
}
