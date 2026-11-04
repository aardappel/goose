// error: field value of extern struct Item is at offset 1, where C places it at 4: write pad 3 before it
// An extern struct's bytes are the C struct's: its pads must put each field
// where C does (spec §7.10).
extern struct Item { tag: u8, value: i32 }
extern fn item_total(it: Item) -> i32;
fn main() { print(item_total(Item { tag: 1, value: 2 })); }
