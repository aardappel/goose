// error: pool.apply takes a regular fn, not thread_fn worker
thread_fn worker(x: i64) {
    print(x);
}

fn main() {
    var pool = thread_pool(2);
    var data: i64[>..] = [1, 2, 3, 4];
    pool.apply(chunks(data, 2), worker);
}
