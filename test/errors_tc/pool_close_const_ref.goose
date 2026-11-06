// error: cannot close a thread_pool through a non-writable value
// A const reference to a pool applies it, but cannot end it: close() writes
// the variable the pool is in.
fn fill(chunk: i64[:]) {
    for &x in chunk { x = 1; }
}

fn finish(p: const thread_pool&, data: i64[:]) {
    p.apply(chunks(data, 1), fill);
    p.close();
}

fn main() {
    var pool = thread_pool(2);
    var data: i64[>..] = [0, 0];
    finish(pool, data);
    print(data);
}
