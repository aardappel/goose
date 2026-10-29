// error: still used
import slot_pool;
fn main() {
    var pool = slot_pool<i64> {};
    let r .= pool.alloc_ref(1);
    pool.items.clear();
    print(r);
}
