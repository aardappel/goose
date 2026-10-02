// error: cannot clear items while n is still used: it may hold a reference or slice into it; it was read out of h and may point into buf or items
// r may be h or h2, so a store through it lands in either, and h may hold
// a view of items (§5.1, §9.5).
var items: u8[>..];
struct H { s: const u8[:] }
struct R { h: H& }
fn main() {
    items.append("abcd");
    var buf: u8[..8] = "xy";
    var h = H { s: buf[..] };
    var h2 = H { s: buf[..] };
    var rs: R[>..] = [R { h: h }, R { h: h2 }];
    let r: H& = rs[if items.len > 2 { 0 } else { 1 }].h;
    r.s = items[0..2];
    let n = h.s;
    items.clear();
    items.append("ZZZZ");
    print(n);
}
