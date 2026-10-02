// error: cannot clear items while n is still used: it may hold a reference or slice into it; it was read out of h and may point into buf or items
// The elements resize adds hold the fill, a view of items (§5.1, §9.5).
var items: u8[>..];
fn main() {
    items.append("abcd");
    var buf: u8[..8] = "xy";
    var h: const u8[:][>..] = [buf[..]];
    h.resize(3, items[0..2]);
    let n = h[2];
    items.clear();
    items.append("ZZZZ");
    print(n);
}
