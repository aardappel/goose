// runtime-debug: fresh slots, LIFO reuse and references across allocation/free.
import slot_pool;

fn main() {
    var pool = slot_pool<i64> {};
    let first .= pool.alloc_ref(10);
    let second = pool.alloc_index(20);
    let third = alloc_index(pool, 30);
    pool.free(second);
    free(pool, third);
    print(pool.alloc_index(40), " ", pool.alloc_index(50), " ", pool.items);
    pool.free(pool.items.index_of(first));
    let replacement .= alloc_ref(pool, 60);
    print(first, " ", replacement, " ", pool.items.len, " ", pool.free_slots.len);
}
