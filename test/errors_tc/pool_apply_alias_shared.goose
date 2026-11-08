// error: shared argument to apply() cannot alias data, which is mutably partitioned by chunks()
fn bad_kernel(chunk: i64[:], shared: const i64[:]) {
    for &x, i in chunk {
        x = shared[i];
    }
}

fn main() {
    var pool = thread_pool(2);
    var data: i64[>..] = [1, 2, 3, 4];
    pool.apply(chunks(data, 2), bad_kernel, data);
}
