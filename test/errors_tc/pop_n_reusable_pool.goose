// error: cannot pop_n reusable pool pool: its slots stay live for the freelist
// A reusable pool's freelist keeps every slot live, so it never shrinks (§5.4).
reusable var pool: i64[>..] = [];
fn main() {
    pool.alloc_index(3);
    pool.pop_n(1);
}
