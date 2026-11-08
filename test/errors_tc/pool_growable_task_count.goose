// error: chunks(out, num_tasks) requires an i64 task count, got f64
fn keep(out: i64[>..]&, idx: i64) {
    out.push(idx);
}

fn main() {
    var pool = thread_pool(2);
    defer pool.close();
    var out: i64[>..] = [];
    pool.apply(chunks(out, 2.0), keep);
}
