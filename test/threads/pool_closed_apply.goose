// close() ends a thread_pool: it joins the pool's workers and releases their
// data stacks, and the variable holds a closed pool from then on. Closing it
// again does nothing, and applying it is a runtime error.
fn fill(chunk: i64[:], idx: i64) {
    for &x in chunk { x = idx + 1; }
}

fn main() {
    var pool = thread_pool(2);
    var data: i64[>..] = [0, 0, 0, 0];
    pool.apply(chunks(data, 2), fill);
    print(data);
    pool.close();
    close(pool);
    print("closed");
    pool.apply(chunks(data, 2), fill);
    print(data);
}
