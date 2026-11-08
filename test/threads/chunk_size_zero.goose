// chunks(data, size) cuts data into runs of size elements, one or more: a
// size of zero would never get past the first run.
fn fill(chunk: i64[:]) {
    for &x in chunk { x = 1; }
}

fn main() {
    var pool = thread_pool(2);
    var data: i64[>..] = [0, 0, 0, 0];
    pool.apply(chunks(data, 0), fill);
    print(data);
}
