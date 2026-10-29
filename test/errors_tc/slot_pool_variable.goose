// error: arrays of variable-size elements cannot be indexed
import slot_pool;
fn main() {
    var pool = slot_pool<u8[]> {};
    pool.alloc_index("text");
}
