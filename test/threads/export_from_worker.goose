// A worker's C calling the program's export is a runtime error (§7.11), as
// main's is: Goose code is running on the worker's thread, and the export
// would run on its stack block. The sidecar's header gives call_back, C
// that calls the export.
extern fn call_back(n: i64) -> i64;

export "on_worker_callback" fn on_callback(n: i64) -> i64 { n + 1 }

thread_fn worker() { print(call_back(1)); }

fn main() {
    print("main ", on_callback(1));
    thread_wait(thread_spawn(worker));
}
