// runtime-debug: a library call in a builder block names the block's line, not the allocation's.
import slot_pool;
fn main() {
    var pool = slot_pool<i64> {};
    var other = slot_pool<i64> {};
    pool.alloc_with() {
        other.free(3);
        5
    };
    print("unreachable");
}
