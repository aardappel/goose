// error: cannot assign through this path (const, or a read-only instantiation, §9.5)
fn bad_writer(chunk: i64[:], shared: const i64[:]) {
    shared[0] = chunk[0];
}

fn main() {
    var pool = thread_pool(2);
    var data:   i64[>..] = [1, 2, 3, 4];
    var shared: i64[>..] = [10, 20];
    pool.apply(chunks(data, 2), bad_writer, shared);
}
