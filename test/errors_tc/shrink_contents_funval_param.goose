// error: cannot clear items while n is still used: it may hold a reference or slice into it; it was read out of h
// The block stores each's parameter's elements into h, views of items as
// this call passes them (§5.1, §7.5, §9.5).
var items: u8[>..];
fn each<F>(xs: (const u8[:])[:]) { for i in xs.len { F(xs[i]); } }
fn main() {
    items.append("abcd");
    var buf: u8[..8] = "xy";
    var h: const u8[:][>..] = [buf[..]];
    var vs: (const u8[:])[>..] = [items[0..2]];
    each(vs) { w => h.push(w); };
    let n = h[1];
    items.clear();
    items.append("ZZZZ");
    print(n);
}
