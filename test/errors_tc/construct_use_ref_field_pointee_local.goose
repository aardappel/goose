// error: cannot use d.own in the value assigned to d.r
// The same on a local whose reference field was bound to its own tail (§4.4).
fn twice(xs: const u8[:]) -> u8[>..<] {
    var r: u8[>..<] = [];
    r.append(xs);
    r.append(xs);
    return r;
}
struct D { n: i64, r: u8[>..<]&, own: u8[>..<] }
fn main() {
    var x: u8[>..<] = [];
    var d = D { n: 0, r: x, own: [1, 2, 3] };
    d.r .= d.own;
    d.r = twice(d.own);
    print(d.own);
}
