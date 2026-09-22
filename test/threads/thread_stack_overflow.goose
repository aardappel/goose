// A worker's recursion deeper than its native stack holds ends the program
// with the same diagnostic as the main thread's (§9.3): a worker sets up the
// runtime's fault reporting for its own stack as it starts. Expected to
// abort.

recursive fn sink(n: i64) -> i64 {
    if n < 0 { return 0; }                          // Never true.
    let a: i64[4] = [n, n * 3 + 1, n ^ 5, n * 7];
    let r = sink(a[n % 4] % 1000 + 1);
    r + a[r % 4]
}

thread_fn worker(n: i64) { print(sink(n)); }

fn main() {
    print("descending in a worker");
    thread_wait(thread_spawn(worker, 1));           // Aborts.
}
