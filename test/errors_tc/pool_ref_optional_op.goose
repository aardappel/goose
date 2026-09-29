// error: .alloc_index exists on reusable pools only: o may point at pool, but is not a pool reference
// Only a plain reference carries a pool's freelist (§5.4): an optional one
// bound to a pool is an optional reference to an array.
fn main() {
    reusable var pool: i64[>..] = [1, 2];
    var o: i64[>..]&? = pool;
    if o { print(o.alloc_index(3)); }
}
