// A worker's program has its own copy of every global it uses, taken from
// the spawning instance at spawn (§11.2): what a worker writes stays in the
// worker, and what main wrote before the spawn is what the worker starts
// from. Deterministic: every worker is waited for before the next spawn.
struct Cfg { n: i64, name: u8[8] }
var counter = 0;
var log: u8[>..] = [];
let table: i64[3] = [1, 2, 3];
var cfg = Cfg { n: 1, name: "startup " };
var text: u8[] = "shared?";
reusable var pool: i64[>..] = [];
const LIMIT = 100;

fn touch(k: i64) {
    counter += 10 * k;                   // the worker's own counter
    log.push('w');                       // and its own log
    let r = &table;
    r[0] = k;                            // a let global's contents, through a reference
    cfg.n = k;
    let slot = pool.alloc_index(k);      // a pool, with its freelist
    if slot > 0 { pool.free(0); }
}

thread_fn worker(k: i64) {
    touch(k);
    print("worker ", k, ": ", counter, " ", log, " ", table, " ", cfg.n, " ", text, " ",
          pool.len, " ", LIMIT);
    if k == 2 {
        // A worker's worker copies the worker's globals, not main's.
        thread_wait(thread_spawn(worker, 3));
    }
}

fn main_thread_globals() {
    counter = 5;
    log.append("main");
    pool.alloc_index(0);
    print("main: ", counter, " ", log, " ", table, " ", cfg.n, " ", text, " ", pool.len);
    thread_wait(thread_spawn(worker, 1));
    print("main: ", counter, " ", log, " ", table, " ", cfg.n, " ", text, " ", pool.len);
    counter = 7;
    thread_wait(thread_spawn(worker, 2));
    print("main: ", counter, " ", log, " ", table, " ", cfg.n, " ", text, " ", pool.len);
}

fn main() { main_thread_globals(); }
