// error: cannot call shrink, which shrinks msgs: msgs[1], evaluated earlier in the statement, may still refer into msgs (§5.2)
// apply_contained appends to its message buffer once its later arguments
// have run, as a member operation writes its receiver: none of them may
// shrink the array the buffer is in.
fn refuse(chunk: i64[:], n: i64) {
    assert(chunk.len == n);
}

fn shrink(xs: u8[..8][>..<]&) -> i64 {
    xs.pop();
    0
}

fn main() {
    var pool = thread_pool(1);
    defer pool.close();
    var data: i64[>..]      = [0, 0];
    var msgs: u8[..8][>..<] = [[], []];
    print(pool.apply_contained(chunks(data, 1), msgs[1], refuse, shrink(msgs)));
}
