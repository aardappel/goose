// error: cannot call run: it shrinks d while h is still used, and h may refer into d (§5.1)
// The same where the assignment is through a reference parameter: the call
// judges what the holder it passes refers to (§4.4, §5.1).
struct D { n: i64, tail: u8[>..] }
struct H { d: D& }
fn total(h: H&) -> u8[>..] {
    var r: u8[>..] = [];
    r.append(h.d.tail);
    return r;
}
fn run(d: D&, h: H&) { d.tail = total(h); }
fn main() {
    var d = D { n: 1, tail: [1, 2] };
    var h = H { d: d };
    run(d, h);
    print(d.tail);
}
