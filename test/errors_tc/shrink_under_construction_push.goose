// error: cannot call drop_items, which shrinks xs: the element pushed into xs is still under construction, and the shrink would free the storage it is built in
// A variable-size element is built at the array's top (§4.3), which the
// shrink moves down under it (§1.3).
struct Item { name: u8[], n: i64 }
fn drop_items(xs: Item[>..]&) -> i64 { xs.clear(); return 0; }
fn main() {
    var xs: Item[>..] = [];
    xs.push(Item { name: "a", n: 1 });
    xs.push(Item { name: "b", n: drop_items(xs) });
    print(xs.len);
}
