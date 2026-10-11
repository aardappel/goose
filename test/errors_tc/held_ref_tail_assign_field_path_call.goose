// error: cannot call run: it shrinks d while h is still used, and h may refer into d (§5.1)
// The same where the assignment is through a reference parameter (§4.4,
// §5.1).
struct D { n: i64, tail: u8[>..] }
struct H { n: i64, d: D& }
fn total(d: D&) -> u8[>..] {
    var r: u8[>..] = [];
    r.append(d.tail);
    return r;
}
fn run(d: D&, h: H&) { d.tail = total(h.d); }
fn main() {
    var d = D { n: 1, tail: [1, 2] };
    var h = H { n: 2, d: d };
    run(d, h);
    print(d.tail);
}
