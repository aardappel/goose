// runtime-debug: a bad free index fails before the builder runs.
import slot_pool;
fn main() {
    var pool = slot_pool<i64> {};
    pool.alloc_ref(1);
    pool.free_slots.push(99);
    pool.alloc_with() { print("builder ran"); 2 };
}
