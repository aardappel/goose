// runtime-debug: a known global owner retains the check before builder effects.
import slot_pool;
var pool = slot_pool<i64> {};
fn take(p: slot_pool<i64>&) -> i64, i64 {
    let r: i64& = p.alloc_with() { print("builder ran"); 2 };
    return r, p.items.len;
}
fn main() {
    pool.items.push(1);
    pool.free_slots.push(99);
    take(pool);
    print("unreachable");
}
