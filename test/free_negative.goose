// runtime-debug: negative indices abort before entering the freelist.
fn bad_index() -> i64 { print("index evaluated"); return -1; }
fn main() {
    reusable var pool: i64[>..] = [1];
    pool.free(bad_index());
    print("unreachable");
}
