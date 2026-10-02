// error: cannot clear items while n is still used: it may hold a reference or slice into it; it was read out of h and may point into buf or items
// put stores a view of items through the reference to h that r is
// (§5.1, §9.5).
var items: u8[>..];
struct H { s: const u8[:] }
fn put(h: H&) { h.s = items[0..2]; }
fn main() {
    items.append("abcd");
    var buf: u8[..8] = "xy";
    var h = H { s: buf[..] };
    let r = &h;
    put(r);
    let n = h.s;
    items.clear();
    items.append("ZZZZ");
    print(n);
}
