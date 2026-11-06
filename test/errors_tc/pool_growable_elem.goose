// error: pool.apply(keep, ...): first parameter out of keep must be i64[>..]&, got i32[>..]&
// A task's output is appended to the array: its elements are the array's.
fn keep(out: i32[>..]&, idx: i64) {
    out.push(1);
}

fn main() {
    var pool = thread_pool(2);
    defer pool.close();
    var out: i64[>..] = [];
    pool.apply(chunks(out, 2), keep);
}
