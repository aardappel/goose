// error: cannot clear items while n is still used
// h holds static data until put stores a view of items there, so what is
// read out of it after may view items (§5.1).
var items: u8[>..];
fn put(x: const u8[:][>..]&) { x.push(items[0..1]); }
fn main() {
    items.push(97);
    var h: const u8[:][>..] = ["a"];
    put(h);
    let n = h[1];
    items.clear();
    print(n);
}
