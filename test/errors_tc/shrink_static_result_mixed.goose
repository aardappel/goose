// error: cannot call reset, which shrinks items while m is still used
// One return gives static data, the other a view of items (§5.1).
var items: u8[>..];
fn pick(x: i64) -> const u8[:] {
    if x > 0 { return "a"; }
    return items[0..1];
}
fn reset() { items.clear(); }
fn main() {
    items.push(97);
    let m = pick(0);
    reset();
    print(m);
}
