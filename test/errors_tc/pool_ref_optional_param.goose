// error: .alloc_index exists on reusable pools only
// An optional parameter given a pool is not a pool reference either (§5.4).
fn f(p: i64[>..]&?) -> i64 {
    if p { return p.alloc_index(1); }
    return -1;
}
fn main() {
    reusable var pool: i64[>..] = [1];
    print(f(pool));
}
