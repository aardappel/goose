// error: cannot clear items while n is still used: it may hold a reference or slice into it; it was read out of h and may point into buf or items
// Assigning h whole stores what o holds, a view of items (§5.1, §9.5).
var items: u8[>..];
struct H { s: const u8[:] }
fn main() {
    items.append("abcd");
    var buf: u8[..8] = "xy";
    var h = H { s: buf[..] };
    let o = H { s: items[0..2] };
    h = o;
    let n = h.s;
    items.clear();
    items.append("ZZZZ");
    print(n);
}
