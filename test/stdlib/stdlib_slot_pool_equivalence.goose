// runtime-debug all-levels: differential allocation/free traces against unchanged reusable.
import slot_pool;

struct Item { key: i64, stamp: i64 }
fn next(r: u64) -> u64 { r * 6364136223846793005 + 1442695040888963407 }

fn main() {
    var pool = slot_pool<Item> {};
    reusable var builtin: Item[>..] = [];
    var live: i64[>..<] = [];
    var rng: u64 = 12345;
    let kept .= pool.alloc_ref(Item { -1, 99 });
    let bkept .= builtin.alloc_ref(Item { -1, 99 });
    for n in 4000 {
        rng = next(rng);
        if live.len != 0 && rng % 3 == 0 {
            let at = ((rng >> 16) % live.len as u64) as i64;
            let index = live[at];
            pool.free(index);
            builtin.free(index);
            live[at] = live[live.len - 1];
            live.pop();
        } else {
            let value = Item { n, (rng % 1000000) as i64 };
            let index = pool.alloc_index(value);
            let bindex = builtin.alloc_index(value);
            assert(index == bindex);
            live.push(index);
        }
        assert(pool.items.len == builtin.len);
        assert(pool.items == builtin);
        assert(kept.stamp == bkept.stamp);
    }
    // Free every active slot, then consume the complete freelist in LIFO order.
    while live.len != 0 {
        let index = live.pop();
        pool.free(index);
        builtin.free(index);
    }
    let high = pool.items.len;
    for i in 1..high {
        let a .= pool.alloc_ref(Item { i, i * 2 });
        let b .= builtin.alloc_ref(Item { i, i * 2 });
        assert(pool.items.index_of(a) == builtin.index_of(b));
    }
    assert(pool.items.len == high && pool.free_slots.len == 0);
    assert(pool.items == builtin && kept.key == -1);
    print("equivalent");
}
