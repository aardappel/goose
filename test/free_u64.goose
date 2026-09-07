// runtime-debug: a full-width unsigned index cannot wrap into a valid slot.
fn main() {
    reusable var pool: i64[>..] = [1];
    let i: u64 = 18446744073709551615;
    pool.free(i);
    print("unreachable");
}
