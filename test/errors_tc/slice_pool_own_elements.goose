// error: slice_pool.alloc_slice: a run of elements that hold own values is not supported: keep them in a slot_pool, one handle each
// A run has no text per element in which each could be emptied.
import slice_pool;
struct Buf { n: i64 }
struct Conn { id: i64, input: own Buf? }
var runs = slice_pool<Conn> {};
fn main() {
    let r = runs.take_slice(2);
    runs.release(r);
}
