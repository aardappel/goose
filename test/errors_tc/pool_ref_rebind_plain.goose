// error: re-binding lp to a reference that is not a pool reference, where it was bound to a reusable pool
// A reference bound to a pool carries the pool's freelist (§5.4), which a
// rebind to an array that is not a pool cannot give it: alloc_index below
// would hand out pool's free slots in other.
fn main() {
    reusable var pool: i64[>..] = [1, 2];
    var other: i64[>..] = [5, 6];
    var lp: i64[>..]& = pool;
    lp .= other;
    print(lp.alloc_index(3));
    print(other.len);
}
