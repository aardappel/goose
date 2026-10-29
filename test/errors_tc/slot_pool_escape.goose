// error: returning a reference rooted in pool.items
import slot_pool;
fn escape() -> i64& {
    var pool = slot_pool<i64> {};
    pool.alloc_ref(1)
}
fn main() { print(escape()); }
