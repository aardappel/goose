// error: cannot clear items while n is still used: it may hold a reference or slice into it; it was read out of h and may point into buf or items
// The block each runs stores a view of items into h, the frame it is
// written in, beside the view of buf h held (§5.1, §9.5).
var items: u8[>..];
fn each<F>(n: i64) { for i in n { F(i); } }
fn main() {
    items.append("abcd");
    var buf: u8[..8] = "xy";
    var h: const u8[:][>..] = [buf[..]];
    each(1) { i => h.push(items[0..2]); };
    let n = h[1];
    items.clear();
    items.append("ZZZZ");
    print(n);
}
