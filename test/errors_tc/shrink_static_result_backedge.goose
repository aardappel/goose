// error: cannot clear items while r is still used
// The entry call gives `s` static data, which the body returns, but the
// back edge passes it a view of items and reuses the body (§7.8): the
// result is static data only for the entry call, so r may view items.
var items: u8[>..];
recursive fn f(s: const u8[:], n: i64) -> const u8[:] {
    if n == 0 { return s; }
    let r = f(items[0..2], n - 1);
    items.clear();
    print(r);
    return "x";
}
fn main() {
    items.push(97);
    items.push(98);
    print(f("lit", 1));
}
