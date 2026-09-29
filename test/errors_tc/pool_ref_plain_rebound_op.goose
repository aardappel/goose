// error: .alloc_index exists on reusable pools only: r may point at pool, but is not a pool reference
// A reference first bound to an array that is not a pool stays a plain one
// when .= points it at a pool (§5.4): it has no freelist to allocate from.
fn main() {
    reusable var pool: i64[>..] = [1, 2];
    var other: i64[>..] = [5, 6];
    var r: i64[>..]& = other;
    r .= pool;
    print(r.alloc_index(3));
}
