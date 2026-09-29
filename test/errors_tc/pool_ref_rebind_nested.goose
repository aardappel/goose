// error: re-binding r to a reference that is not a pool reference, where it was bound to a reusable pool
// A nested function rebinds the variable it names outside itself (§7.5),
// which is the pool reference its enclosing function allocates through.
fn main() {
    reusable var pool: i64[>..] = [1, 2];
    var other: i64[>..] = [3];
    var r: i64[>..]& = pool;
    fn away() { r .= other; }
    away();
    print(r.alloc_index(4));
}
