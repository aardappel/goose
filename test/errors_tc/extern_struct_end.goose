// error: extern struct Item is 12 bytes, which C rounds up to 16: write pad 4 at its end
// C rounds a struct's size up to its alignment, which an array of them and
// a call by value keep (spec §7.10).
extern struct Item { weight: f64, value: i32 }
extern fn item_total(it: Item) -> f64;
fn main() { print(item_total(Item { weight: 1.0, value: 2 })); }
