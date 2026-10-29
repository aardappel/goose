// error: read-only
import slot_pool;
fn change(pool: const slot_pool<i64>&) { pool.alloc_index(1); }
fn main() {
    var pool = slot_pool<i64> {};
    change(pool);
}
