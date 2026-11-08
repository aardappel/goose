// error: cannot append through a non-writable value (let, or non-writable provenance, §9.5)
// The message is appended to the buffer, which a const reference cannot do.
fn fill(chunk: i64[:]) {
    for &x in chunk { x = 1; }
}

fn tried(pool: thread_pool&, data: i64[:], msg: const u8[>..]&) -> bool {
    pool.apply_contained(chunks(data, 1), msg, fill)
}

fn main() {
    var pool = thread_pool(2);
    var data: i64[>..] = [0, 0];
    var msg:  u8[>..]  = [];
    print(tried(pool, data, msg));
}
