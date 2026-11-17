// Views read out of a field of a held handle are ordinary values while the
// handle is held: used, deferred releases judged where they run, and
// rebinding that ends what the view answers to.
import slice_pool;
import slot_pool;
struct Conn { input: own u8[:] }
var bytes = slice_pool<u8> {};
var conns = slot_pool<Conn> {};
fn go() {
    let h = conns.take(Conn { input: bytes.take_slice(8) });
    let r: u8[:] = h.input;
    defer conns.release(h) { bytes.release(take(it.input)); };
    print(r[..]);                      // the defer releases at this block's end
}
fn main() { go(); }
