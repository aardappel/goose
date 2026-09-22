// A `var` global of a const type can be assigned as a whole (§9.5), so
// every program instance has its own, copied at spawn like any var global
// (§11.2); only `let` and `const` globals are static data all instances
// share. Deterministic: every worker is waited for before the next spawn.
var level: const i64 = 3;
var table: const i64[3] = [1, 2, 3];
const LIMIT = 100;
let SCALE: const i64 = 7;

thread_fn worker(k: i64) {
    level += k;
    table = [k, k, k];
    print("worker ", k, ": ", level, " ", table, " ", LIMIT, " ", SCALE);
}

fn main() {
    print("main: ", level, " ", table);
    thread_wait(thread_spawn(worker, 1));
    print("main: ", level, " ", table);
    level = 10;
    thread_wait(thread_spawn(worker, 2));
    print("main: ", level, " ", table);
}
