// runtime-debug: invalid slots abort before they can become unsafe references.
import slot_pool;
fn bad_index() -> i64 { print("index evaluated"); return -1; }
fn main() {
    var pool = slot_pool<i64> {};
    pool.alloc_index(1);
    pool.free(bad_index());
    print("unreachable");
}
