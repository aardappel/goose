// goose-threads
// In a contained worker (§11.2), an abort in an iteration of a par for ends
// that worker, not the program, once every chunk has ended
// (docs/design/child_threads.md §4.7), and the worker's thread_wait takes
// the iteration's message. A worker whose par for returns runs on.
struct Sum { n: i64 }

thread_fn worker(scale: i64) {
    let xs = [1, 2, 3];
    var out: i64[40] = [0; 40];
    par for k in 40 { out[k] = xs[k * scale / 20]; }
    var t = 0;
    for v in out { t += v; }
    qput(Sum { n: t });
}

// The message without its location, which names the file as it was built.
fn located(msg: u8[:]) -> i64 {
    var i = msg.len;
    while i > 0 && msg[i - 1] != '(' { i--; }
    if i < 2 { return msg.len; }
    i - 2
}

fn main() {
    print("returns: ", thread_wait(thread_spawn_contained(worker, 1)), " ", qget<Sum>().n);
    var why: u8[>..] = [];
    print("aborts: ", thread_wait(thread_spawn_contained(worker, 2), why), " ",
          why[..located(why)]);
    print("main runs on");
}
