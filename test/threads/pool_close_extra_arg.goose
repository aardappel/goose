// An apply's arguments may close its pool (pool_close_chunk_size.goose):
// here an extra argument for the worker, evaluated before any chunk is
// handed out, closes it.
fn scale(chunk: i64[:], by: i64) {
    for &x in chunk { x = x * by; }
}

fn close_then_factor(p: thread_pool&) -> i64 {
    p.close();
    return 3;
}

fn main() {
    var p = thread_pool(2);
    var xs: i64[>..] = [1, 2, 3, 4];
    print("applying");
    p.apply(chunks(xs, 2), scale, close_then_factor(p));
    print(xs);
}
