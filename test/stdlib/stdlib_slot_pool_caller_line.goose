// runtime-debug: a check failing inside the standard library names the line of the program's own code that called it.
import slot_pool;
fn release(p: slot_pool<i64>&, i: i64) { p.free(i); }
fn main() {
    var pool = slot_pool<i64> {};
    pool.alloc_index(1);
    release(pool, 0);
    print("released");
    release(pool, 5);
    print("unreachable");
}
