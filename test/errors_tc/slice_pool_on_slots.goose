// error: .alloc_slice exists on reusable[] pools only, not on the slot pools of reusable
// Slices come from reusable[] pools only; a reusable pool's freelist holds single slots (§5.4).
fn main() {
    reusable var pool: i64[>..] = [];
    let s = pool.alloc_slice(2);
    print(s);
}
