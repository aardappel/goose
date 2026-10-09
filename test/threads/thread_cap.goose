// runtime-define: GS_STACK_BUDGET=4294967296
// hardware_threads() reports no more workers than the data stack address
// space budget holds beside the main program (§11.2), and at least one. A
// 4 GB budget holds two regions at the default reservation, fewer than any
// program with a worker takes, so the cap is that floor wherever this runs;
// the budget plans rather than refuses, so the one worker it allows still
// gets its stacks.

thread_fn worker(n: i64) {
    var parts: u8[>..] = [];
    for i in n { parts.append("ab"); }
    qput(parts.len);
}

fn main() {
    print(hardware_threads());          // 1
    let id = thread_spawn(worker, 3);
    print(qget<i64>());                 // 6
    thread_wait(id);
}
