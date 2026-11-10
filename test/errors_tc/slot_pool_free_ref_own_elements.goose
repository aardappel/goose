// error: slot_pool.free_ref: an element that holds own values is given up with release, whose block consumes what it owns
// A handle borrowed as the reference it is does not free its slot.
import slice_pool;
import slot_pool;
struct Conn { id: i64, input: own u8[:] }
var bytes = slice_pool<u8> {};
var conns = slot_pool<Conn> {};
fn main() {
    let a = conns.take(Conn { id: 1, input: bytes.take_slice(64) });
    conns.free_ref(a);
    conns.release(a) { bytes.release(take(it.input)); };
}
