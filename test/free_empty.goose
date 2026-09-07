// runtime-debug: an empty pool has no slot zero.
fn main() {
    reusable var pool: i64[>..] = [];
    pool.free(0);
    print("unreachable");
}
