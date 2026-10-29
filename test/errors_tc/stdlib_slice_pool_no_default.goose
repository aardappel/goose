// error: has no declared default
import slice_pool;
struct Item { value: i64& }
fn main() { var p = slice_pool<Item> {}; p.alloc_slice(1); }
