// error: cannot use global thread_pool shared inside a worker (reached from pool.apply worker fn fill)
// error: cannot use global thread_pool spare inside a worker (reached from thread_fn worker)
// error: cannot use global thread_pool shared inside a worker (reached from pool.apply worker fn refill)
// A worker's program copies the globals it uses (§11.2), but a thread_pool is
// a handle to the threads of the program that made it: fill, running on
// shared, would wait for shared's tasks to finish, its own among them. A `let`
// pool is no different from a `var` one, and the worker's callees count too.
// So does a worker only an export applies: nothing calls run, which is
// checked with the functions nothing reached, and refill's program with it.
let shared = thread_pool(2);
var spare = thread_pool(2);

fn inner(chunk: i64[:]) {
    for &x in chunk { x = 1; }
}

fn fill(chunk: i64[:]) {
    shared.apply(chunks(chunk, 1), inner);
}

fn helper(data: i64[:]) {
    spare.apply(chunks(data, 1), inner);
}

thread_fn worker() {
    var data: i64[>..] = [0, 0];
    helper(data);
}

fn refill(chunk: i64[:]) {
    shared.apply(chunks(chunk, 1), inner);
}

export fn run() {
    var data: i64[>..] = [0, 0, 0, 0];
    shared.apply(chunks(data, 2), refill);
}

fn main() {
    var data: i64[>..] = [0, 0, 0, 0];
    shared.apply(chunks(data, 2), fill);
    thread_wait(thread_spawn(worker));
    print(data);
}
