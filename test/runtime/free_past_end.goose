// runtime-debug: an index equal to the length is already out of bounds.
fn main() {
    reusable var pool: i64[>..] = [1, 2];
    pool.free(2);
    print("unreachable");
}
