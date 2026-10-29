// error: storing a reference
import slot_pool;
struct Entry { value: i64& }
fn main() {
    var pool = slot_pool<Entry> {};
    {
        var short = 3;
        pool.alloc_index(Entry { short });
    }
    print(pool.items[0]);
}
