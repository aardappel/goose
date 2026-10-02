// error: cannot clear items while n is still used: it may hold a reference or slice into it; it was read out of h and may point into buf or items
// Appending o copies its views of items into h (§5.1, §9.5).
var items: u8[>..];
fn main() {
    items.append("abcd");
    var buf: u8[..8] = "xy";
    var h: const u8[:][>..] = [buf[..]];
    var o: const u8[:][>..] = [items[0..2]];
    h.append(o);
    let n = h[1];
    items.clear();
    items.append("ZZZZ");
    print(n);
}
