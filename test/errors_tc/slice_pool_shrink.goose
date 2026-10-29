// error: cannot clear pool.items while s is still used
import slice_pool;
fn main() {
    var pool = slice_pool<i64> {};
    let s = pool.alloc_slice(2);
    pool.items.clear();
    print(s);
}
