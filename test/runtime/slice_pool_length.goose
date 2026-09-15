// runtime-debug: a negative slice pool length aborts before the pool changes.
fn main() {
    reusable[] var pool: i64[>..] = [];
    let s = pool.alloc_slice(2);
    print(pool.len);
    let t = pool.realloc_slice(s, s.len - 3);
    print("unreachable ", t.len);
}
