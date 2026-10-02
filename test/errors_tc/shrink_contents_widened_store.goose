// error: cannot clear items while n is still used: it may hold a reference or slice into it; it was read out of h and may point into buf or items
// put stores through the reference its holder parameter holds, which may
// be h or h2, so both hold what it stored (§5.1, §9.2, §9.5).
var items: u8[>..];
struct R { h: (const u8[:])[>..]& }
fn put(r: R) { r.h.push(items[0..2]); }
fn main() {
    items.append("abcd");
    var buf: u8[..8] = "xy";
    var h: const u8[:][>..] = [buf[..]];
    var h2: const u8[:][>..] = [buf[..]];
    put(R { h: if items.len > 2 { h } else { h2 } });
    let n = h[1];
    items.clear();
    items.append("ZZZZ");
    print(n);
}
