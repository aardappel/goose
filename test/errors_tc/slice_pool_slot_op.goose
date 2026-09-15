// error: .alloc_index exists on reusable pools only, not on the slice pools of reusable[]
// A reusable[] pool hands out slices, not single slots (§5.4).
fn main() {
    reusable[] var pool: i64[>..] = [];
    let i = pool.alloc_index(1);
    print(i);
}
