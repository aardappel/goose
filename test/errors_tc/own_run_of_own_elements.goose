// error: a run of such elements is not supported
// A run's elements have no text each that could say every one was emptied.
struct Buf { n: i64 }
struct Task { id: i64, buf: own Buf? }
trusted fn drop_tasks(ts: own Task[:]) {}
fn main() {}
