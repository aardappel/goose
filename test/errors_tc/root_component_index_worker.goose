// error: thread programs cannot copy root global state
root struct State { items: i32[>..] = [] }
var state = State {};
type Id = u32 in state.items;
thread_fn worker() { print(0 as Id); }
fn main() {
    state.items.push(1);
    thread_wait(thread_spawn(worker));
}
