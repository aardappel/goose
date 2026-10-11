// error: cannot assign d while h is still used: a reference into it was stored there
// The right-hand side reads a holder's field that refers to the struct
// whose tail is rebuilt, and follows it into the array being built
// (§4.4, §5.1).
struct D { n: i64, tail: u8[>..] }
struct H { n: i64, d: D& }
fn total(d: D&) -> u8[>..] {
    var r: u8[>..] = [];
    r.append(d.tail);
    return r;
}
fn main() {
    var d = D { n: 1, tail: [1, 2] };
    var h = H { n: 2, d: d };
    d.tail = total(h.d);
    print(d.tail);
}
