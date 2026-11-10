// error: slot_pool.free: an element that holds own values is given up with release, whose block consumes what it owns
// A slot freed by index is reused as it is: the run its element still owns
// would be overwritten there, never released (docs/design/ownership.md 10).
import slice_pool;
import slot_pool;
struct Conn { id: i64, input: own u8[:] }
var bytes = slice_pool<u8> {};
var conns = slot_pool<Conn> {};
fn main() {
    let a = conns.take(Conn { id: 1, input: bytes.take_slice(64) });
    conns.free(conns.items.index_of(a));
    conns.release(a) { bytes.release(take(it.input)); };
}
