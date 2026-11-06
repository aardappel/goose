// error: chunks() requires a writable array or slice
// The tasks' output is appended to the array, which a const reference
// cannot do.
fn keep(out: i64[>..]&, idx: i64) {
    out.push(idx);
}

fn fill(pool: thread_pool&, out: const i64[>..]&) {
    pool.apply(chunks(out, 2), keep);
}

fn main() {
    var pool = thread_pool(2);
    defer pool.close();
    var out: i64[>..] = [];
    fill(pool, out);
}
