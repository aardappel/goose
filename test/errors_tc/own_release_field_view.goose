// error: cannot consume h while r is still used
// A slice read out of a field of a held handle answers to the handle: the
// release frees the run for its reuse, so the view dies with the release
// (§4.8 of docs/design/ownership.md).
import slice_pool;
import slot_pool;
struct Conn { input: own u8[:] }
var bytes = slice_pool<u8> {};
var conns = slot_pool<Conn> {};
fn main() {
    let h = conns.take(Conn { input: bytes.take_slice(8) });
    let r: u8[:] = h.input;            // a plain read: a borrow of the field's run
    conns.release(h) { bytes.release(take(it.input)); };
    print(r[..]);
}
