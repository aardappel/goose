// error: thread programs cannot copy root global state
root struct State { first: i64[>..], second: i64[>..] }
var state: State;
fn mutate() { state.first.push(1); }
thread_fn worker() { mutate(); }
fn main() { thread_wait(thread_spawn(worker)); }
