// The thread applying a pool is the last of its workers
// (docs/design/parallel_blocks.md §5): a pool of n workers has n - 1 threads
// of its own and a pool of one has none, so main runs each of solo's chunks
// here while it waits for them. It runs each as a pool thread would: on data
// stacks the pool keeps for it, with a copy of the globals the worker uses,
// taken as the apply hands the chunk out, and an argument built for the
// chunk. A task's own pool, whose chunks it runs in turn, works the same
// within it. Main finds its own stacks and globals as it left them, and a
// pool of three workers, whichever of them runs what, gives the same.

var calls: i64 = 0;
var tag: u8[>..] = [];

fn bump(chunk: i64[:], idx: i64) {
    for &x in chunk { x = x + idx; }
}

fn tally(chunk: i64[:], idx: i64, base: i64, word: u8[>..]) {
    calls += 1;             // the task's copy, not main's
    tag.append(word);
    var scratch: i64[>..] = [];
    for i in idx + 1 { scratch.push(i); }
    var inner = thread_pool(2);
    defer inner.close();
    inner.apply(chunks(chunk, 1), bump);
    for &x in chunk {
        x = x + base + idx * 100 + calls * 10 + scratch.len + tag.len * 1000;
    }
}

fn run_on(pool: thread_pool&) {
    var data: i64[>..] = [];
    data.resize(6, 0);
    var kept: i64[>..] = [7, 8, 9];
    calls = 5;
    tag.resize(0);
    tag.append("ab");
    var word: u8[>..] = [];
    word.append("xyz");
    pool.apply(chunks(data, 2), tally, 100000, copy(word));
    kept.push(10);
    tag.append("c");
    print(data);
    print("calls ", calls, ", tag ", tag, ", kept ", kept);
}

fn main() {
    var solo = thread_pool(1);
    defer solo.close();
    run_on(solo);
    var three = thread_pool(3);
    defer three.close();
    run_on(three);
}
