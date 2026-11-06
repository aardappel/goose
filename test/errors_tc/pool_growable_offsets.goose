// error: chunks() of a growable output parameter T[>..]& takes a task count (i64), not an offsets slice
// A worker that grows an output of its own runs once per task: there is
// nothing for offsets to cut out of the array its output is appended to.
fn keep(out: i64[>..]&, idx: i64) {
    out.push(idx);
}

fn main() {
    var pool = thread_pool(2);
    defer pool.close();
    var out: i64[>..] = [];
    let offsets = [0, 1, 2];
    pool.apply(chunks(out, offsets), keep);
}
