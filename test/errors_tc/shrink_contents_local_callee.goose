// error: cannot clear items while n is still used: it may hold a reference or slice into it; it was read out of h and may point into buf or items
// put stores a view of items into h beside the view of buf it held, so
// what is read out of it after may view either (§5.1, §9.5).
var items: u8[>..];
fn put(x: const u8[:][>..]&) { x.push(items[0..2]); }
fn main() {
    items.append("abcd");
    var buf: u8[..8] = "xy";
    var h: const u8[:][>..] = [buf[..]];
    put(h);
    let n = h[1];
    items.clear();
    items.append("ZZZZ");
    print(n);
}
