// A task of pool.apply_contained that applies a pool of its own is not
// contained while that pool has tasks out: they may still be writing into
// the task's storage, which the end of a contained task would give the next
// one (docs/design/parallel_blocks.md §5). So an abort of the task's own then
// ends the program, as one on main's thread would: here the second of the
// inner apply's chunks is out of bounds, with the first handed out. A worker
// thread_spawn_contained starts is the same. Expected to abort.
fn fill_idx(chunk: i64[:], idx: i64) {
    for &x in chunk { x = idx; }
}

fn apply_inner(chunk: i64[:]) {
    var inner = thread_pool(2);
    defer inner.close();
    var data: i64[>..] = [0, 0, 0, 0];
    let bad: i64[3] = [0, 2, 1];
    inner.apply(chunks(data, bad), fill_idx);
    chunk[0] = data[0];
}

fn main() {
    var pool = thread_pool(2);
    defer pool.close();
    var results: i64[>..] = [0, 0];
    var why:     u8[>..]  = [];
    print("applying");
    print("not reached: ", pool.apply_contained(chunks(results, 1), why, apply_inner), " ", why);
}
