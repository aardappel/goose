// A formatting hook is part of the worker program and uses its copied globals.
struct Show { x: i64 }
var value = 42;
fn update() { value += 1; }
fn format(out: u8[>..]&, item: Show) {
    update();
    out.append("worker");
}
thread_fn worker() { print(Show { x: 1 }); print(value); }
fn main() {
    thread_wait(thread_spawn(worker));
    print(value);
}
