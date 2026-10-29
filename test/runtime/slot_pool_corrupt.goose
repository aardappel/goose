// runtime-debug: invalid slots abort before they can become unsafe references.
import slot_pool;
fn main() {
    var pool = slot_pool<i64> {};
    pool.alloc_index(1);
    pool.free_slots.push(99);
    pool.alloc_index(2);
    print("unreachable");
}
