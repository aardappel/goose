// error: pool.apply extra parameter other of bad_kernel must be flat or const (`const T&` / `const T[:]`), not i64[:]
fn bad_kernel(chunk: i64[:], other: i64[:]) {
    for &x in chunk { x = other[0]; }
}

fn main() {
    var pool = thread_pool(2);
    var data:  i64[>..] = [1, 2, 3, 4];
    var other: i64[>..] = [10, 20];
    pool.apply(chunks(data, 2), bad_kernel, other);
}
