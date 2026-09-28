// error: cannot call drop_items, which shrinks xs: the run appended to xs is still under construction, and the shrink would free the storage it is built in
// The same for a literal run appended (§4.3, §1.3).
struct Item { name: u8[], n: i64 }
fn drop_items(xs: Item[>..]&) -> i64 { xs.clear(); return 0; }
fn main() {
    var xs: Item[>..] = [];
    xs.append([Item { name: "a", n: 1 }, Item { name: "b", n: drop_items(xs) }]);
    print(xs.len);
}
