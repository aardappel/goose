// error: thread programs may access only flat globals
// error: G holds references (reached from thread_fn worker)
// A variant type met before its enum leaves the enum's other variants to
// look at: E.W holds a relative reference here, after S's E.V field.
struct Cell { v: i64 }
var cells: Cell[>..] = [];
enum E { V { x: i64 }, W { r: Cell&<u16 in cells>? } }
struct S { a: E.V, b: E }
let G: S = S { a: E.V { x: 1 }, b: E.V { x: 2 } };
thread_fn worker(n: i64) { print(G.a.x); }
fn main() { thread_wait(thread_spawn(worker, 1)); }
