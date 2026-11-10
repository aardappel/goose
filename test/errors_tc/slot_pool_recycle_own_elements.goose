// error: slot_pool.recycle_all: elements that hold own values are given up with release_all, whose block consumes what each owns
// Every slot made available again at once, with what the elements own
// still in them.
import slice_pool;
import slot_pool;
struct Conn { id: i64, input: own u8[:] }
var bytes = slice_pool<u8> {};
var conns = slot_pool<Conn> {};
fn main() { conns.recycle_all(); }
