// error: cannot clear items while n is still used: it may hold a reference or slice into it; it was read out of h and may point into buf or items
// The store at the end of the body reaches the read at its start on the
// next iteration, which then may view items (§5.1, §9.5).
var items: u8[>..];
fn main() {
    items.append("abcd");
    var buf: u8[..8] = "xy";
    var h: const u8[:][>..] = [buf[..], buf[..]];
    for i in 2 {
        let n = h[1];
        items.clear();
        items.append("ZZZZ");
        print(n);
        h[1] = items[0..2];
    }
}
