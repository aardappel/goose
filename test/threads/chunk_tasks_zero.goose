// chunks(out, n) for a worker that grows an output of its own hands out n
// tasks, one or more: a count of zero or less aborts at the apply, before
// any task is out.
fn keep(out: i64[>..]&, idx: i64) {
    out.push(idx);
}

fn main() {
    var pool = thread_pool(2);
    var out: i64[>..] = [];
    var n = 2;
    pool.apply(chunks(out, n), keep);
    print(out);
    n -= 2;
    pool.apply(chunks(out, n), keep);
    print(out);
}
