// error: apply in a par for
// R3 (§4.3): no pool's tasks are handed out from a par for's body.
fn work(chunk: i64[:]) { for &x in chunk { x += 1; } }

fn main() {
    var pool = thread_pool(2);
    var data: i64[>..] = [1, 2, 3, 4];
    par for k in 2 { pool.apply(chunks(data, 2), work); }
    pool.close();
}
