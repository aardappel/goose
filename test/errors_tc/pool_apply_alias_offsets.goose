// error: chunks() offsets cannot alias data, which is mutably partitioned by chunks()
fn fill(chunk: i64[:]) {
    for &x in chunk { x = 1; }
}

fn main() {
    var pool = thread_pool(2);
    var data: i64[>..] = [0, 2, 4];
    pool.apply(chunks(data, data), fill);
}
