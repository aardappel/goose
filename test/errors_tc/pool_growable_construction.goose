// error: cannot append to out: the value assigned to out is still under construction
// error: cannot call fill, which grows out: the run appended to out is still under construction, and the growth would land inside it (§1.3)
// The apply appends its tasks' output to the array, directly or in a
// callee, which may not happen while a value is being built in it.
fn keep(out: i64[>..]&, idx: i64) {
    out.push(idx);
}

fn fill(pool: thread_pool&, out: i64[>..]&) -> i64[>..] {
    pool.apply(chunks(out, 2), keep);
    return [1];
}

fn assigned(pool: thread_pool&) {
    var out: i64[>..] = [];
    out = { pool.apply(chunks(out, 2), keep); [1, 2] };
    print(out);
}

fn appended(pool: thread_pool&) {
    var out: i64[>..] = [];
    out.append(fill(pool, out));
    print(out);
}

fn main() {
    var pool = thread_pool(2);
    defer pool.close();
    assigned(pool);
    appended(pool);
}
