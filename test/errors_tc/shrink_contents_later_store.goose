// error: cannot clear items while n is still used: it may hold a reference or slice into it; it was read out of h and may point into buf or items
// h held only a view of buf until the second push stored one of items:
// what is read out of it after may view either (§5.1, §9.5).
var items: u8[>..];
fn main() {
    items.append("abcd");
    var buf: u8[..8] = "xy";
    var h: const u8[:][>..] = [];
    h.push(buf[..]);
    h.push(items[0..2]);
    let n = h[1];
    items.clear();
    items.append("ZZZZ");
    print(n);
}
