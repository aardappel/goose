// error: cannot consume h while e is still used
// A reference to an element of the field's run is one of its views too.
import slice_pool;
import slot_pool;
struct Conn { input: own u8[:] }
var bytes = slice_pool<u8> {};
var conns = slot_pool<Conn> {};
fn main() {
    let h = conns.take(Conn { input: bytes.take_slice(8) });
    let e: u8& = h.input[0];
    conns.release(h) { bytes.release(take(it.input)); };
    print(e);
}
