// error: re-binding lp to a reference to a reusable[] pool, where it was bound to a reusable pool
// A pool reference keeps its pool's kind (§5.4): a reusable[] pool's freelist
// holds runs, not the single slots alloc_index takes.
fn main() {
    reusable var pool: i64[>..] = [1, 2];
    reusable[] var spool: i64[>..] = [5, 6];
    var lp: i64[>..]& = pool;
    lp .= spool;
    print(lp.alloc_index(3));
}
