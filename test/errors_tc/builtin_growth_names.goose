// error: cannot push into a: the element pushed into a is still under construction
// error: cannot append to b: the element pushed into b is still under construction
// error: cannot format into s: the value assigned to s is still under construction
// error: cannot allocate in p: the element allocated in p is still under construction
// error: cannot append to out: the value assigned to out is still under construction
// error: cannot append to msg: the value assigned to msg is still under construction
// Each builtin that grows an array, by the name the builtin table gives its
// growth, growing the array a value is being built in (§1.3(4)): the
// receiver, or the `out` array to_bytes and thread_wait append to.
struct Node { v: i64, nxt: Node&<u32>? }
thread_fn worker() { }
fn pushes() {
    var a: u8[][>..] = [];
    a.push({ a.push("x"); "y" });
}
fn appends() {
    var b: u8[][>..] = [];
    b.push({ b.append(["x"]); "y" });
}
fn formats() {
    var s: u8[>..] = "";
    s = { s.format(1); "abc" };
}
fn allocates() {
    reusable var p: Node[>..] = [];
    p.alloc_index(Node { v: { p.alloc_index(Node { v: 1, nxt: null }); 2 }, nxt: null });
}
fn images() {
    var out: u8[>..] = "";
    let a: i64[] = [1, 2];
    out = { a.to_bytes(out); "abc" };
}
fn waits() {
    var msg: u8[>..] = "";
    let id = thread_spawn_contained(worker);
    msg = { thread_wait(id, msg); "abc" };
}
fn main() {
    pushes();
    appends();
    formats();
    allocates();
    images();
    waits();
}
