// error: pool.apply(count, ...): first parameter out of count must be i64[>..]&, got const i64[>..]&
// A task's output is an array of its own, which it grows: a read-only one
// would stay empty.
fn count(out: const i64[>..]&, idx: i64) {
    print(out.len);
}

fn main() {
    var pool = thread_pool(2);
    defer pool.close();
    var out: i64[>..] = [];
    pool.apply(chunks(out, 2), count);
}
