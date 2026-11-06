// error: apply_contained(chunks(...), msg, fn, ...) appends to a growable u8 array, not i64[>..]
fn fill(chunk: i64[:]) {
    for &x in chunk { x = 1; }
}

fn main() {
    var pool = thread_pool(2);
    var data: i64[>..] = [0, 0];
    var msg:  i64[>..] = [];
    pool.apply_contained(chunks(data, 1), msg, fill);
}
