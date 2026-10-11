// error: cannot assign d while h is still used: a reference into it was stored there
// The right-hand side of a whole assignment runs while the array is
// rebuilt, so a holder's reference to the struct holding it, which a clear
// would leave valid, leads into the array being built (§4.4, §5.1).
struct D { n: i64, tail: u8[>..] }
struct H { d: D& }
fn total(h: H) -> u8[>..] {
    var r: u8[>..] = [];
    r.append(h.d.tail);
    return r;
}
fn main() {
    var d = D { n: 1, tail: [1, 2] };
    let h = H { d: d };
    d.tail = total(h);
    print(d.tail);
}
