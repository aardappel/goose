// A worker thread_spawn_contained starts is not contained while a pool it
// applies has tasks out (docs/design/parallel_blocks.md §5): an abort of its
// own then ends the program, rather than the worker, whose storage the end
// would release under the tasks still writing their chunks of it. Here the
// last of the chunks is out of bounds, with the others handed out. Expected
// to abort.
fn fill_idx(chunk: i64[:], idx: i64) {
    for &x in chunk { x = idx; }
}

thread_fn applier() {
    var pool = thread_pool(3);
    defer pool.close();
    var data: i64[>..] = [0, 0, 0, 0];
    let bad: i64[4] = [0, 1, 2, 1];
    pool.apply(chunks(data, bad), fill_idx);
}

fn main() {
    print("applying");
    var why: u8[>..] = [];
    print("not reached: ", thread_wait(thread_spawn_contained(applier), why), " ", why);
}
