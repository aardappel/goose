// error: shared argument to apply() cannot alias out, which apply() appends its tasks' output to
fn copy_some(out: i64[>..]&, idx: i64, src: const i64[:]) {
    out.push(src[idx]);
}

fn main() {
    var pool = thread_pool(2);
    defer pool.close();
    var out: i64[>..] = [1, 2];
    pool.apply(chunks(out, 2), copy_some, out);
}
